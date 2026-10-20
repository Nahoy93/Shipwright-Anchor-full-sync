#pragma once

#ifdef __cplusplus

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <SDL2/SDL_net.h>
#include <nlohmann/json.hpp>

// TLS state of a connection (mbedTLS), see Network.cpp
struct NetworkTlsState;

class Network {
  private:
    IPaddress networkAddress;
    TCPsocket networkSocket = nullptr;
    UDPsocket udpSocket = nullptr;
    UDPpacket* udpReceivePacket = nullptr;
    UDPpacket* udpSendPacket = nullptr;
    uint16_t udpPort = 0;
    SDLNet_SocketSet socketSet = nullptr;
    std::thread receiveThread;
    std::string receivedData;
    NetworkTlsState* tls = nullptr;
    std::chrono::steady_clock::time_point lastReceiveTime;

    void ReceiveFromServer();
    void HandleRemoteData(char payload[512]);
    void HandleRemoteJson(std::string payload);
    bool OpenConnection();
    void CloseConnection();
    int ReceiveTcp(char* buffer, int length);
    bool HasBufferedTlsData();
    bool StartTls();
    void StopTls();

  protected:
    // Set by a subclass before Enable(): wrap the connection in TLS
    bool useTls = false;
    // Called on the network thread once the TLS handshake is done, with the SHA-256 fingerprint of the server's
    // certificate (upper case hex pairs separated by colons). Return false to refuse the server.
    virtual bool OnTlsCertificate(const std::string& fingerprint);
    // Called on the network thread when the TLS handshake fails (a server without TLS, a broken connection). The
    // connection is tried again as usual.
    virtual void OnTlsFailed(const std::string& reason);
    // Called on the network thread on every pass of its loop while connected
    virtual void OnNetworkTick();
    // Called on the network thread for every UDP datagram that arrives
    virtual void OnUdpDatagram(const uint8_t* data, size_t length);
    // The connection is dropped (and then made again) when nothing has arrived for this long. 0 = never.
    std::atomic<uint32_t> receiveTimeoutMs{ 0 };
    // A UDP socket towards the server's address, on the given port. Network thread only.
    bool OpenUdp(uint16_t port);
    void CloseUdp();
    bool SendUdp(const uint8_t* data, size_t length);
    bool IsUdpOpen() const {
        return udpSocket != nullptr;
    }
    // Sends the bytes as they are (through TLS when it's active). With TLS, only from the network thread.
    bool SendRawToRemote(const char* data, size_t length);
    // The server's TCP port
    uint16_t RemotePort() const {
        return SDLNet_Read16(&networkAddress.port);
    }
    // Feeds a JSON packet that arrived another way (UDP) through the normal incoming path
    void HandleIncomingJsonText(const std::string& payload) {
        HandleRemoteJson(payload);
    }

  public:
    std::atomic<bool> isEnabled{ false };
    std::atomic<bool> isConnected{ false };
    std::atomic<bool> isTlsActive{ false };

    // Traffic counters, TCP and UDP together
    std::atomic<uint64_t> bytesSent{ 0 };
    std::atomic<uint64_t> bytesReceived{ 0 };
    std::atomic<uint64_t> packetsSent{ 0 };
    std::atomic<uint64_t> packetsReceived{ 0 };

    void Enable(const char* host, uint16_t port);
    void Disable();
    /**
     * Raw data handler
     *
     * If you are developing a new remote, you should probably use the json methods instead. This
     * method requires you to parse the data and ensure packets are complete manually, we cannot
     * gaurentee that the data will be complete, or that it will only contain one packet with this
     */
    virtual void OnIncomingData(char payload[512]);
    /**
     * Json handler
     *
     * This method will be called when a complete json packet is received. All json packets must
     * be delimited by a null terminator (\0).
     */
    virtual void OnIncomingJson(nlohmann::json payload);
    virtual void OnConnected();
    virtual void OnDisconnected();
    virtual void ProcessOutgoingPackets();
    void SendDataToRemote(const char* payload);
    virtual void SendJsonToRemote(nlohmann::json packet);
};

#endif // __cplusplus
