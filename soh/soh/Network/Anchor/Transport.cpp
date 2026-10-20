#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "Anchor.h"
#include <ship/Context.h>
#include <ship/window/Window.h>
#include "soh/Notification/Notification.h"

/**
 * Transport: everything between the packets and the connection
 *
 * SERVER_INFO   The server's answer to the handshake: what it supports. Older servers don't send it, and then the
 *               client works exactly like before (one TCP connection, one packet per target client, no pings).
 *               {"type":"SERVER_INFO","features":["targets","ping","udp"],"udp":{"port":0,"token":123,"key":"..."}}
 *                - targets: a packet may name several clients in "targetClientIds", the server makes the copies
 *                - ping:    the server answers PING with PONG and the ping and hearts of every client in the room
 *                - udp:     realtime packets may use the UDP channel described in UdpChannel.h, on "port" (0 = the
 *                           same port number as the TCP connection). The key is only sent over TLS; without TLS the
 *                           channel is not encrypted, like the TCP connection then.
 * PING / PONG   Every 5 seconds: {"type":"PING","t":<ms>,"rtt":..,"hp":..,"maxHp":..,"down":..} and
 *               {"type":"PONG","t":<the same ms>,"clients":{"<id>":[pingMs, health, healthCapacity]}}.
 *               Once the server answers pings, a connection that stays silent for 15 seconds is made again.
 * UDP_HELLO     Over UDP, every second until the server answers with UDP_ACK, then every 5 seconds. "ready" tells
 *               the server that UDP works both ways, so it may send realtime packets to us over UDP as well.
 *
 * Realtime packets (positions, enemy snapshots, sounds) go over UDP once it works, other packets always over TCP.
 * When nothing arrives over UDP for 15 seconds, realtime packets go over TCP again until the server answers.
 */

using Clock = std::chrono::steady_clock;

static int64_t SteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

bool Anchor_IsRealtimePacket(const std::string& packetType) {
    return packetType == Anchor::PLAYER_UPDATE || packetType == Anchor::PLAYER_SFX ||
           packetType == Anchor::OCARINA_SFX || packetType == Anchor::ENEMY_SYNC || packetType == Anchor::PROJECTILES ||
           packetType == Anchor::DROP_MOVE;
}

// MARK: - Network thread

void Anchor::ResetTransportState() {
    serverSupportsTargets = false;
    serverSupportsPing = false;
    receiveTimeoutMs = 0;
    rttMs = -1;
    lastPingSent = {};

    CloseUdp();
    udpCodec.SetKey({});
    udpToken = 0;
    udpSendSequence = 0;
    udpReceiveSequence = 0;
    udpReady = false;
    udpWanted = false;
    udpHellosUnanswered = 0;
    udpLastHello = {};
    udpLastReceive = {};
    udpState = 0;
}

void Anchor::HandleServerInfo(const nlohmann::json& payload) {
    std::vector<std::string> features;
    if (payload.contains("features") && payload["features"].is_array()) {
        for (auto& feature : payload["features"]) {
            if (feature.is_string()) {
                features.push_back(feature.get<std::string>());
            }
        }
    }
    auto has = [&features](const char* name) {
        return std::find(features.begin(), features.end(), name) != features.end();
    };

    serverSupportsTargets = has("targets");
    serverSupportsPing = has("ping");
    // The server answers every ping, so a connection that stays silent this long is dead
    receiveTimeoutMs = serverSupportsPing ? 15000 : 0;
    // Ping right away, so the room window shows a ping without waiting
    lastPingSent = {};

    SPDLOG_INFO("[Anchor] Server features: {}", payload.contains("features") ? payload["features"].dump() : "[]");

    CloseUdp();
    udpWanted = false;
    udpReady = false;
    udpState = 0;

    if (!has("udp") || !settingUseUdp || !payload.contains("udp") || !payload["udp"].is_object()) {
        return;
    }

    const nlohmann::json& udp = payload["udp"];
    uint16_t port = udp.contains("port") && udp["port"].is_number_unsigned() ? udp["port"].get<uint16_t>() : 0;
    // 0 = the same port as the TCP connection (works through port forwarding that changes the port number)
    if (port == 0) {
        port = RemotePort();
    }
    uint32_t token = udp.contains("token") && udp["token"].is_number_unsigned() ? udp["token"].get<uint32_t>() : 0;
    std::string keyHex = udp.contains("key") && udp["key"].is_string() ? udp["key"].get<std::string>() : "";
    std::vector<uint8_t> key = AnchorUdp::HexDecode(keyHex);

    // Over TLS the datagrams must be encrypted too, otherwise UDP would leak what TLS protects
    if (port == 0 || token == 0 || (isTlsActive && key.size() != AnchorUdp::KEY_SIZE) ||
        (!keyHex.empty() && key.size() != AnchorUdp::KEY_SIZE)) {
        SPDLOG_WARN("[Anchor] Not using UDP: the server's UDP details are incomplete");
        return;
    }
    if (!udpCodec.SetKey(key)) {
        SPDLOG_WARN("[Anchor] Not using UDP: this build can't use the server's UDP encryption");
        return;
    }
    if (!OpenUdp(port)) {
        return;
    }

    udpToken = token;
    udpSendSequence = 0;
    udpReceiveSequence = 0;
    udpHellosUnanswered = 0;
    udpWanted = true;
    udpState = 1;
    udpLastReceive = Clock::now();
    SendUdpHello();
}

void Anchor::HandlePongTiming(const nlohmann::json& payload) {
    if (!payload.contains("t") || !payload["t"].is_number_integer()) {
        return;
    }
    int64_t roundTrip = SteadyMs() - payload["t"].get<int64_t>();
    if (roundTrip >= 0 && roundTrip < 60000) {
        rttMs = (int)roundTrip;
    }
}

bool Anchor::SendUdpPayload(const std::string& payload) {
    if (!udpReady || !IsUdpOpen() || payload.size() > AnchorUdp::MAX_PAYLOAD) {
        return false;
    }

    static thread_local std::vector<uint8_t> datagram;
    // Every sealed datagram uses a new sequence number, the nonce may never repeat for a key
    uint64_t sequence = ++udpSendSequence;
    if (!udpCodec.Seal(AnchorUdp::ClientToServer, udpToken, sequence, payload, datagram)) {
        return false;
    }
    return SendUdp(datagram.data(), datagram.size());
}

void Anchor::SendUdpHello() {
    udpLastHello = Clock::now();
    if (!IsUdpOpen()) {
        return;
    }

    nlohmann::json hello = { { "type", UDP_HELLO }, { "ready", udpReady } };
    std::vector<uint8_t> datagram;
    uint64_t sequence = ++udpSendSequence;
    if (udpCodec.Seal(AnchorUdp::ClientToServer, udpToken, sequence, hello.dump(), datagram)) {
        SendUdp(datagram.data(), datagram.size());
    }
    if (!udpReady) {
        udpHellosUnanswered++;
    }
}

void Anchor::OnNetworkTick() {
    if (!isConnected) {
        return;
    }

    Clock::time_point now = Clock::now();

    if (serverSupportsPing && now - lastPingSent >= std::chrono::seconds(5)) {
        lastPingSent = now;
        nlohmann::json ping = {
            { "type", PING },
            { "t", SteadyMs() },
            { "rtt", rttMs.load() },
            { "hp", statusHealth.load() },
            { "maxHp", statusHealthCapacity.load() },
            { "down", statusDown.load() },
            { "quiet", true },
        };
        std::string text = ping.dump();
        text.push_back('\0');
        if (SendRawToRemote(text.data(), text.size())) {
            packetsSent++;
        }
    }

    if (!udpWanted) {
        return;
    }

    // Quickly at first, then slower: a firewall that drops UDP shouldn't get a datagram every second forever
    auto helloInterval = (udpReady || udpHellosUnanswered >= 10) ? std::chrono::seconds(5) : std::chrono::seconds(1);
    if (now - udpLastHello >= helloInterval) {
        SendUdpHello();
    }

    if (udpReady && now - udpLastReceive > std::chrono::seconds(15)) {
        // The server answers every hello, so the datagrams stopped arriving (a router forgot the mapping)
        SPDLOG_WARN("[Anchor] Nothing received over UDP for 15 seconds, using TCP until UDP works again");
        udpReady = false;
        udpHellosUnanswered = 0;
        udpState = 1;
    }
}

void Anchor::OnUdpDatagram(const uint8_t* data, size_t length) {
    if (!udpWanted) {
        return;
    }

    uint32_t token = 0;
    uint64_t sequence = 0;
    std::string text;
    if (!udpCodec.Open(AnchorUdp::ServerToClient, data, length, token, sequence, text) || token != udpToken) {
        return;
    }
    // Late or repeated datagrams: the newer one already arrived
    if (sequence <= udpReceiveSequence) {
        return;
    }
    udpReceiveSequence = sequence;
    udpLastReceive = Clock::now();

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Anchor] Failed to parse a UDP packet: {}", e.what());
        return;
    }
    if (!payload.is_object() || !payload.contains("type") || !payload["type"].is_string()) {
        return;
    }

    std::string packetType = payload["type"].get<std::string>();
    if (packetType == UDP_ACK) {
        if (!udpReady) {
            SPDLOG_INFO("[Anchor] UDP works, movement updates use it now");
            udpReady = true;
            udpState = 2;
            udpHellosUnanswered = 0;
            // Tells the server that it can send realtime packets to us over UDP too
            SendUdpHello();
        }
        return;
    }

    // Only packets that may be lost come this way
    if (!Anchor_IsRealtimePacket(packetType)) {
        return;
    }

    packetsReceived++;
    try {
        OnIncomingJson(payload);
    } catch (const std::exception& e) { SPDLOG_ERROR("[Anchor] Exception handling a UDP packet: {}", e.what()); }
}

bool Anchor::SimulateIncomingPacket(const std::string& packetType, Clock::time_point& releaseAt) {
    int loss = simLossPercent;
    int latency = simLatencyMs;
    int jitter = simJitterMs;
    if (loss <= 0 && latency <= 0 && jitter <= 0) {
        return true;
    }

    static thread_local std::mt19937 random{ std::random_device{}() };

    // Like UDP: only packets where the next one replaces a lost one get lost
    if (loss > 0 && Anchor_IsRealtimePacket(packetType) && (int)(random() % 100) < loss) {
        return false;
    }

    int delay = std::max(0, latency);
    if (jitter > 0) {
        delay += (int)(random() % (uint32_t)(jitter + 1));
    }
    releaseAt += std::chrono::milliseconds(delay);
    return true;
}

static bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static bool IsFingerprintSeparator(char c) {
    return c == ':' || c == '-' || c == ' ';
}

// The SHA-256 fingerprint in "AB:CD:..." form (32 pairs), or empty when the text isn't one. Accepts the pairs with
// ':', '-' or a space between them or nothing at all, in any case, and also the whole line copied from the server
// window ("... TLS certificate fingerprint: AB:CD:...").
std::string Anchor_ParseFingerprint(const std::string& text) {
    std::string rest = text;
    std::string lower;
    for (char c : rest) {
        lower.push_back(c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
    }
    size_t label = lower.rfind("fingerprint:");
    if (label != std::string::npos) {
        rest = rest.substr(label + 12);
    }

    size_t first = rest.find_first_not_of(" \t\r\n");
    size_t last = rest.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    rest = rest.substr(first, last - first + 1);

    std::string fingerprint;
    size_t i = 0;
    for (int pair = 0; pair < 32; pair++) {
        if (pair > 0) {
            if (i < rest.size() && IsFingerprintSeparator(rest[i])) {
                i++;
            }
            fingerprint.push_back(':');
        }
        if (i + 1 >= rest.size() || !IsHexDigit(rest[i]) || !IsHexDigit(rest[i + 1])) {
            return "";
        }
        for (size_t j = i; j < i + 2; j++) {
            char c = rest[j];
            fingerprint.push_back(c >= 'a' && c <= 'f' ? (char)(c - 'a' + 'A') : c);
        }
        i += 2;
    }

    // Nothing may follow: a longer run of digits isn't a SHA-256 fingerprint
    return i == rest.size() ? fingerprint : "";
}

bool Anchor::OnTlsCertificate(const std::string& fingerprint) {
    std::lock_guard<std::mutex> lock(tlsMutex);
    std::string expected = Anchor_ParseFingerprint(tlsExpectedFingerprint);

    // A pinned fingerprint that can't be read must not turn into "trust anything"
    if (expected.empty() && tlsExpectedIsPinned) {
        SPDLOG_ERROR("[Anchor] The pinned fingerprint \"{}\" isn't a SHA-256 fingerprint", tlsExpectedFingerprint);
        tlsError = "The server fingerprint in the settings isn't valid: it should be 32 pairs like AB:CD:..., as the "
                   "server window shows it";
        isEnabled = false;
        return false;
    }

    if (expected.empty()) {
        // First connection to this server: trust it and remember the certificate (like SSH does)
        tlsExpectedFingerprint = fingerprint;
        tlsLearnedFingerprint = fingerprint;
        SPDLOG_INFO("[Anchor] First connection to {}, remembering its certificate {}", tlsHostKey, fingerprint);
        return true;
    }

    if (expected == fingerprint) {
        return true;
    }

    SPDLOG_ERROR("[Anchor] The certificate of {} is {}, expected {}", tlsHostKey, fingerprint, expected);
    tlsError = tlsExpectedIsPinned ? "The server's certificate doesn't match the fingerprint in the settings"
                                   : "The server's certificate has changed. If the server was set up again, press "
                                     "\"Forget Saved Certificates\" and connect again.";
    // Stop trying, the game thread turns Anchor off and shows the error
    isEnabled = false;
    return false;
}

void Anchor::OnTlsFailed(const std::string& reason) {
    std::lock_guard<std::mutex> lock(tlsMutex);
    // Once per Enable: the connection keeps being tried every few seconds
    if (!tlsWarned) {
        tlsWarned = true;
        tlsWarning = reason;
    }
}

// MARK: - Game thread

static nlohmann::json LoadKnownTlsHosts() {
    std::string text = CVarGetString(CVAR_REMOTE_ANCHOR("TlsKnownHosts"), "");
    if (text.empty()) {
        return nlohmann::json::object();
    }
    try {
        nlohmann::json hosts = nlohmann::json::parse(text);
        if (hosts.is_object()) {
            return hosts;
        }
    } catch (...) {}
    return nlohmann::json::object();
}

std::string Anchor::KnownTlsFingerprint(const std::string& hostKey) {
    nlohmann::json hosts = LoadKnownTlsHosts();
    if (hosts.contains(hostKey) && hosts[hostKey].is_string()) {
        return hosts[hostKey].get<std::string>();
    }
    return "";
}

void Anchor::RememberTlsFingerprint(const std::string& hostKey, const std::string& fingerprint) {
    nlohmann::json hosts = LoadKnownTlsHosts();
    hosts[hostKey] = fingerprint;
    CVarSetString(CVAR_REMOTE_ANCHOR("TlsKnownHosts"), hosts.dump().c_str());
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void Anchor::ForgetTlsCertificates() {
    CVarClear(CVAR_REMOTE_ANCHOR("TlsKnownHosts"));
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void Anchor::ProcessTlsEvents() {
    std::string learned;
    std::string error;
    std::string warning;
    std::string hostKey;
    {
        std::lock_guard<std::mutex> lock(tlsMutex);
        learned.swap(tlsLearnedFingerprint);
        error.swap(tlsError);
        warning.swap(tlsWarning);
        hostKey = tlsHostKey;
    }

    if (!warning.empty()) {
        Notification::Emit({
            .prefix = "Anchor",
            .message =
                "Encrypted connection failed (" + warning + "). Does the server have TLS turned on? Still trying...",
            .messageColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
            .remainingTime = 12.0f,
        });
    }

    if (!learned.empty()) {
        RememberTlsFingerprint(hostKey, learned);
        Notification::Emit({
            .prefix = "Anchor",
            .message = "Encrypted connection, the server's certificate is saved",
        });
    }

    if (!error.empty()) {
        Notification::Emit({
            .prefix = "Anchor",
            .message = error,
            .messageColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
            .remainingTime = 15.0f,
        });
        CVarClear(CVAR_REMOTE_ANCHOR("Enabled"));
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        Disable();
    }
}

void Anchor::HandlePacket_Pong(nlohmann::json payload) {
    if (!payload.contains("clients") || !payload["clients"].is_object()) {
        return;
    }

    for (auto& [key, value] : payload["clients"].items()) {
        if (!value.is_array() || value.size() < 3) {
            continue;
        }
        uint32_t clientId = (uint32_t)std::stoul(key);
        auto it = clients.find(clientId);
        if (it == clients.end()) {
            continue;
        }
        it->second.pingMs = value[0].is_number() ? value[0].get<s32>() : -1;
        it->second.health = value[1].is_number() ? value[1].get<s16>() : (s16)-1;
        it->second.healthCapacity = value[2].is_number() ? value[2].get<s16>() : (s16)-1;
    }
}

int Anchor::EstimatedPacketAgeMs(uint32_t senderClientId) {
    // Our half of the way, from the server to us
    int roundTrip = rttMs;
    int ours = roundTrip > 0 ? roundTrip / 2 : 0;
    // The sender's half, from them to the server: their ping when the server told us, otherwise like ours
    int theirs = ours;
    auto it = clients.find(senderClientId);
    if (it != clients.end() && it->second.pingMs > 0) {
        theirs = it->second.pingMs / 2;
    }
    // Plus about half a game update waiting in the queue, and the simulated latency when it's on
    return ours + theirs + 25 + std::max(0, (int)simLatencyMs) + std::max(0, (int)simJitterMs) / 2;
}
