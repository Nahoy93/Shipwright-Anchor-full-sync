#include "Network.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef ENABLE_ANCHOR_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#endif

/**
 * The connection to a remote (Anchor server, Crowd Control, Sail).
 *
 * One thread per remote connects, receives and (for remotes that queue their packets) sends. Packets are JSON text
 * terminated by a null byte. Optional for a remote:
 *  - TLS around the TCP connection (mbedTLS), with the server certificate's fingerprint handed to the remote to check
 *  - a UDP socket towards the same server, for packets where the newest one is all that matters
 *  - a receive timeout, to notice a connection that died without being closed
 * Failed connection attempts are retried with a growing pause (1, 2, 4, then every 5 seconds).
 */

static constexpr int RECEIVE_BUFFER_SIZE = 16 * 1024;
// ReceiveTcp: TLS has no complete data yet, try again later
static constexpr int RECEIVE_NOTHING_YET = -2;
static constexpr int UDP_PACKET_SIZE = 2048;

// MARK: - TLS

#ifdef ENABLE_ANCHOR_TLS
// A server that never answers (one without TLS) or a connection that stalls in the middle of a TLS record
static constexpr uint32_t TLS_READ_TIMEOUT_MS = 10000;

struct NetworkTlsState {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    TCPsocket socket = nullptr;
    // Only the TCP socket, to wait for data with a timeout
    SDLNet_SocketSet waitSet = nullptr;
    // Stop waiting when the remote is turned off
    std::atomic<bool>* keepGoing = nullptr;
};

static int TlsSend(void* ctx, const unsigned char* buf, size_t len) {
    NetworkTlsState* state = (NetworkTlsState*)ctx;
    int sent = SDLNet_TCP_Send(state->socket, buf, (int)len);
    return sent < (int)len ? MBEDTLS_ERR_NET_SEND_FAILED : sent;
}

// Waits in short steps, so turning the remote off (or a server that never answers) doesn't leave the thread stuck
static int TlsRecvTimeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeoutMs) {
    NetworkTlsState* state = (NetworkTlsState*)ctx;
    uint32_t waited = 0;
    while (true) {
        int ready = SDLNet_CheckSockets(state->waitSet, 100);
        if (ready < 0) {
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        if (ready > 0 && SDLNet_SocketReady(state->socket)) {
            break;
        }
        waited += 100;
        if (!*state->keepGoing || (timeoutMs != 0 && waited >= timeoutMs)) {
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
    }

    int received = SDLNet_TCP_Recv(state->socket, buf, (int)len);
    if (received < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return received; // 0 = connection closed
}

// After the handshake: never wait. Waiting here would hold up the network thread (and with it everything we send)
// whenever a TLS record isn't application data (session tickets right after the handshake) or arrives in pieces.
static int TlsRecvNonBlocking(void* ctx, unsigned char* buf, size_t len) {
    NetworkTlsState* state = (NetworkTlsState*)ctx;
    int ready = SDLNet_CheckSockets(state->waitSet, 0);
    if (ready < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (ready == 0 || !SDLNet_SocketReady(state->socket)) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    int received = SDLNet_TCP_Recv(state->socket, buf, (int)len);
    if (received < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return received; // 0 = connection closed
}

static std::string TlsErrorString(int code) {
    char text[160];
    mbedtls_strerror(code, text, sizeof(text));
    return std::string(text) + " (-0x" + fmt::format("{:04X}", -code) + ")";
}

static bool InitPsa() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = psa_crypto_init() == PSA_SUCCESS; });
    return ok;
}
#else
struct NetworkTlsState {};
#endif

bool Network::StartTls() {
#ifdef ENABLE_ANCHOR_TLS
    if (!InitPsa()) {
        SPDLOG_ERROR("[Network] Could not initialise the crypto library");
        return false;
    }

    tls = new NetworkTlsState();
    tls->socket = networkSocket;
    tls->keepGoing = &isEnabled;
    tls->waitSet = SDLNet_AllocSocketSet(1);
    if (tls->waitSet != nullptr) {
        SDLNet_TCP_AddSocket(tls->waitSet, networkSocket);
    }
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_ctr_drbg_init(&tls->drbg);

    static const char personalization[] = "soh-anchor";
    int ret = mbedtls_ctr_drbg_seed(&tls->drbg, mbedtls_entropy_func, &tls->entropy,
                                    (const unsigned char*)personalization, sizeof(personalization) - 1);
    if (ret == 0) {
        ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    }
    if (ret == 0) {
        // Self-hosted servers use a self-signed certificate. Instead of a certificate authority, the remote checks the
        // certificate's fingerprint (pinned or remembered from the first connection), see OnTlsCertificate.
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->drbg);
        mbedtls_ssl_conf_read_timeout(&tls->conf, TLS_READ_TIMEOUT_MS);
        ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    }
    if (ret == 0 && tls->waitSet == nullptr) {
        ret = MBEDTLS_ERR_SSL_ALLOC_FAILED;
    }
    if (ret != 0) {
        SPDLOG_ERROR("[Network] TLS setup failed: {}", TlsErrorString(ret));
        StopTls();
        return false;
    }

    mbedtls_ssl_set_bio(&tls->ssl, tls, TlsSend, nullptr, TlsRecvTimeout);

    while ((ret = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            std::string reason;
            if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
                reason = "the server didn't answer";
            } else if (ret == MBEDTLS_ERR_SSL_CONN_EOF || ret == MBEDTLS_ERR_NET_CONN_RESET) {
                reason = "the server closed the connection";
            } else {
                reason = TlsErrorString(ret);
            }
            SPDLOG_ERROR("[Network] TLS handshake failed: {}", reason);
            StopTls();
            // Unless the remote is being turned off anyway
            if (isEnabled) {
                OnTlsFailed(reason);
            }
            return false;
        }
    }

    const mbedtls_x509_crt* certificate = mbedtls_ssl_get_peer_cert(&tls->ssl);
    if (certificate == nullptr) {
        SPDLOG_ERROR("[Network] The server sent no TLS certificate");
        StopTls();
        return false;
    }

    unsigned char hash[32];
    mbedtls_sha256(certificate->raw.p, certificate->raw.len, hash, 0);
    std::string fingerprint;
    for (int i = 0; i < 32; i++) {
        if (i > 0) {
            fingerprint += ':';
        }
        fingerprint += fmt::format("{:02X}", hash[i]);
    }

    if (!OnTlsCertificate(fingerprint)) {
        SPDLOG_ERROR("[Network] Refused the server's TLS certificate {}", fingerprint);
        StopTls();
        return false;
    }

    // From now on reads only take what has arrived, see TlsRecvNonBlocking
    mbedtls_ssl_set_bio(&tls->ssl, tls, TlsSend, TlsRecvNonBlocking, nullptr);

    SPDLOG_INFO("[Network] TLS connection established ({})", mbedtls_ssl_get_version(&tls->ssl));
    isTlsActive = true;
    return true;
#else
    SPDLOG_ERROR("[Network] This build has no TLS support");
    return false;
#endif
}

void Network::StopTls() {
#ifdef ENABLE_ANCHOR_TLS
    if (tls == nullptr) {
        return;
    }
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ctr_drbg_free(&tls->drbg);
    mbedtls_entropy_free(&tls->entropy);
    if (tls->waitSet != nullptr) {
        SDLNet_FreeSocketSet(tls->waitSet);
    }
#endif
    delete tls;
    tls = nullptr;
    isTlsActive = false;
}

bool Network::HasBufferedTlsData() {
#ifdef ENABLE_ANCHOR_TLS
    return tls != nullptr && mbedtls_ssl_get_bytes_avail(&tls->ssl) > 0;
#else
    return false;
#endif
}

int Network::ReceiveTcp(char* buffer, int length) {
#ifdef ENABLE_ANCHOR_TLS
    if (tls != nullptr) {
        while (true) {
            int ret = mbedtls_ssl_read(&tls->ssl, (unsigned char*)buffer, length);
            if (ret > 0) {
                bytesReceived += ret;
                return ret;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
                // A record that wasn't data (a session ticket) or only part of one: nothing to hand on yet
                return RECEIVE_NOTHING_YET;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
#endif
            if (ret != 0 && ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                SPDLOG_ERROR("[Network] TLS read failed: {}", TlsErrorString(ret));
            }
            return -1;
        }
    }
#endif
    int received = SDLNet_TCP_Recv(networkSocket, buffer, length);
    if (received > 0) {
        bytesReceived += received;
    }
    return received;
}

bool Network::SendRawToRemote(const char* data, size_t length) {
    if (networkSocket == nullptr || length == 0) {
        return networkSocket != nullptr;
    }

#ifdef ENABLE_ANCHOR_TLS
    if (tls != nullptr) {
        size_t written = 0;
        while (written < length) {
            int ret = mbedtls_ssl_write(&tls->ssl, (const unsigned char*)data + written, length - written);
            if (ret > 0) {
                written += ret;
            } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                SPDLOG_ERROR("[Network] TLS write failed: {}", TlsErrorString(ret));
                return false;
            }
        }
        bytesSent += length;
        return true;
    }
#endif

    int sent = SDLNet_TCP_Send(networkSocket, data, static_cast<int>(length));
    if (sent > 0) {
        bytesSent += sent;
    }
    return sent == static_cast<int>(length);
}

// MARK: - UDP

bool Network::OpenUdp(uint16_t port) {
    CloseUdp();

    udpSocket = SDLNet_UDP_Open(0);
    if (udpSocket == nullptr) {
        SPDLOG_ERROR("[Network] SDLNet_UDP_Open: {}", SDLNet_GetError());
        return false;
    }
    udpReceivePacket = SDLNet_AllocPacket(UDP_PACKET_SIZE);
    udpSendPacket = SDLNet_AllocPacket(UDP_PACKET_SIZE);
    if (udpReceivePacket == nullptr || udpSendPacket == nullptr) {
        CloseUdp();
        return false;
    }
    if (socketSet != nullptr) {
        SDLNet_UDP_AddSocket(socketSet, udpSocket);
    }
    udpPort = port;
    return true;
}

void Network::CloseUdp() {
    if (udpSocket != nullptr) {
        if (socketSet != nullptr) {
            SDLNet_UDP_DelSocket(socketSet, udpSocket);
        }
        SDLNet_UDP_Close(udpSocket);
        udpSocket = nullptr;
    }
    if (udpReceivePacket != nullptr) {
        SDLNet_FreePacket(udpReceivePacket);
        udpReceivePacket = nullptr;
    }
    if (udpSendPacket != nullptr) {
        SDLNet_FreePacket(udpSendPacket);
        udpSendPacket = nullptr;
    }
}

bool Network::SendUdp(const uint8_t* data, size_t length) {
    if (udpSocket == nullptr || udpSendPacket == nullptr || length > (size_t)udpSendPacket->maxlen) {
        return false;
    }

    memcpy(udpSendPacket->data, data, length);
    udpSendPacket->len = (int)length;
    udpSendPacket->address.host = networkAddress.host;
    SDLNet_Write16(udpPort, &udpSendPacket->address.port);

    if (SDLNet_UDP_Send(udpSocket, -1, udpSendPacket) == 0) {
        return false;
    }
    bytesSent += length;
    return true;
}

// MARK: - Public

void Network::Enable(const char* host, uint16_t port) {
    if (isEnabled) {
        return;
    }

    // A thread that stopped by itself (a refused server) is joined before isEnabled is set again, otherwise it could
    // see the new value and keep running
    if (receiveThread.joinable()) {
        receiveThread.join();
    }

    if (SDLNet_ResolveHost(&networkAddress, host, port) == -1) {
        SPDLOG_ERROR("[Network] SDLNet_ResolveHost: {}", SDLNet_GetError());
    }

    isEnabled = true;
    receiveThread = std::thread(&Network::ReceiveFromServer, this);
}

void Network::Disable() {
    if (!isEnabled) {
        // The connection thread may have stopped by itself (a refused server), clean it up
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
        return;
    }

    isEnabled = false;
    receiveThread.join();
}

void Network::OnIncomingData(char payload[512]) {
}

void Network::OnIncomingJson(nlohmann::json payload) {
}

void Network::OnConnected() {
}

void Network::OnDisconnected() {
}

void Network::ProcessOutgoingPackets() {
}

bool Network::OnTlsCertificate(const std::string& fingerprint) {
    return true;
}

void Network::OnNetworkTick() {
}

void Network::OnTlsFailed(const std::string& reason) {
}

void Network::OnUdpDatagram(const uint8_t* data, size_t length) {
}

void Network::SendDataToRemote(const char* payload) {
    SPDLOG_DEBUG("[Network] Sending data: {}", payload);
    if (SendRawToRemote(payload, strlen(payload) + 1)) {
        packetsSent++;
    }
}

void Network::SendJsonToRemote(nlohmann::json payload) {
    SendDataToRemote(payload.dump().c_str());
}

// MARK: - Private

bool Network::OpenConnection() {
    networkSocket = SDLNet_TCP_Open(&networkAddress);
    if (!networkSocket) {
        return false;
    }

    if (useTls && !StartTls()) {
        SDLNet_TCP_Close(networkSocket);
        networkSocket = nullptr;
        return false;
    }

    return true;
}

void Network::CloseConnection() {
    CloseUdp();
    if (socketSet != nullptr) {
        SDLNet_FreeSocketSet(socketSet);
        socketSet = nullptr;
    }
#ifdef ENABLE_ANCHOR_TLS
    if (tls != nullptr) {
        mbedtls_ssl_close_notify(&tls->ssl);
    }
#endif
    StopTls();
    if (networkSocket != nullptr) {
        SDLNet_TCP_Close(networkSocket);
        networkSocket = nullptr;
    }
}

void Network::ReceiveFromServer() {
    int failedAttempts = 0;

    while (isEnabled) {
        while (!isConnected && isEnabled) {
            SPDLOG_TRACE("[Network] Attempting to make connection to server...");

            if (OpenConnection()) {
                failedAttempts = 0;
                isConnected = true;
                receivedData.clear();
                lastReceiveTime = std::chrono::steady_clock::now();
                SPDLOG_INFO("[Network] Connection to server established!");

                OnConnected();
                break;
            }

            // Wait a little longer after every failed attempt (1, 2, 4, then every 5 seconds), so a server that's down
            // isn't flooded and this thread doesn't spin
            failedAttempts++;
            int waitMs = std::min(5000, 1000 << std::min(failedAttempts - 1, 3));
            for (int waited = 0; waited < waitMs && isEnabled; waited += 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        if (!isConnected) {
            continue;
        }

        socketSet = SDLNet_AllocSocketSet(2);
        if (socketSet && networkSocket) {
            SDLNet_TCP_AddSocket(socketSet, networkSocket);
        }

        // Listen to socket messages
        while (isConnected && networkSocket && isEnabled) {
            // we check first if socket has data, to not block in the TCP_Recv
            int socketsReady = SDLNet_CheckSockets(socketSet, 0);

            if (socketsReady == -1) {
                SPDLOG_ERROR("[Network] SDLNet_CheckSockets: {}", SDLNet_GetError());
                break;
            }

            // Always process outgoing packets
            ProcessOutgoingPackets();
            OnNetworkTick();

            uint32_t timeoutMs = receiveTimeoutMs;
            if (timeoutMs != 0 &&
                std::chrono::steady_clock::now() - lastReceiveTime > std::chrono::milliseconds(timeoutMs)) {
                SPDLOG_WARN("[Network] Nothing received for {} ms, reconnecting", timeoutMs);
                break;
            }

            if (udpSocket != nullptr && SDLNet_SocketReady(udpSocket)) {
                while (udpSocket != nullptr && SDLNet_UDP_Recv(udpSocket, udpReceivePacket) > 0) {
                    // Only datagrams from the server
                    if (udpReceivePacket->address.host != networkAddress.host ||
                        SDLNet_Read16(&udpReceivePacket->address.port) != udpPort) {
                        continue;
                    }
                    // (lastReceiveTime is only for the TCP connection: it can die while UDP still works)
                    bytesReceived += udpReceivePacket->len;
                    OnUdpDatagram(udpReceivePacket->data, (size_t)udpReceivePacket->len);
                }
            }

            bool tcpReady = (socketsReady > 0 && SDLNet_SocketReady(networkSocket)) || HasBufferedTlsData();
            if (!tcpReady) {
                // No incoming data
                continue;
            }

            static thread_local std::vector<char> remoteDataReceived(RECEIVE_BUFFER_SIZE);
            int len = ReceiveTcp(remoteDataReceived.data(), RECEIVE_BUFFER_SIZE);
            if (len == RECEIVE_NOTHING_YET && networkSocket) {
                continue;
            }
            if (len <= 0 || !networkSocket) {
                SPDLOG_ERROR("[Network] SDLNet_TCP_Recv: {}", SDLNet_GetError());
                break;
            }
            lastReceiveTime = std::chrono::steady_clock::now();

            // The raw data handler gets (at most) the first 511 bytes
            char rawData[512];
            int rawLength = std::min(len, 511);
            memcpy(rawData, remoteDataReceived.data(), rawLength);
            rawData[rawLength] = '\0';
            HandleRemoteData(rawData);

            // Only look for delimiters in the part that just arrived
            size_t searchFrom = receivedData.size();
            receivedData.append(remoteDataReceived.data(), len);

            // Process all complete packets
            size_t packetStart = 0;
            size_t delimiterPos = receivedData.find('\0', searchFrom);
            while (delimiterPos != std::string::npos) {
                HandleRemoteJson(receivedData.substr(packetStart, delimiterPos - packetStart));
                packetStart = delimiterPos + 1;
                delimiterPos = receivedData.find('\0', packetStart);
            }
            if (packetStart > 0) {
                receivedData.erase(0, packetStart);
            }
        }

        bool wasConnected = isConnected;
        CloseConnection();

        if (wasConnected) {
            isConnected = false;
            receivedData.clear();
            OnDisconnected();
            SPDLOG_INFO("[Network] Ending receiving thread...");
        }
    }
}

void Network::HandleRemoteData(char payload[512]) {
    OnIncomingData(payload);
}

void Network::HandleRemoteJson(std::string payload) {
    SPDLOG_DEBUG("[Network] Received json: {}", payload);
    packetsReceived++;
    nlohmann::json jsonPayload;
    try {
        jsonPayload = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Failed to parse json: \n{}\n{}\n", payload, e.what());
        return;
    }

    try {
        OnIncomingJson(jsonPayload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Exception handling incoming JSON: {}", e.what());
    } catch (...) { SPDLOG_ERROR("[Network] Unknown exception handling incoming JSON"); }
}
