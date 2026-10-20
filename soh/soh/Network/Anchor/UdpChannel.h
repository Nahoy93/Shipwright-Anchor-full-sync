#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Datagrams of the Anchor UDP channel, the format the server's udp.go uses as well.
 *
 *   offset  size  field
 *        0     2  magic "AU"
 *        2     1  version (1)
 *        3     1  flags (bit 0: the payload is encrypted)
 *        4     4  token, little endian: identifies the client's connection (the server uses the same one)
 *        8     8  sequence number, little endian, counting up per direction
 *       16     n  payload: a JSON packet, or when encrypted the AES-128-GCM ciphertext followed by its 16 byte tag
 *
 * Encrypted datagrams use the 16 header bytes as additional data and a 12 byte nonce made of the direction (1 = client
 * to server, 2 = server to client), three zero bytes and the sequence number. The key comes from the server over the
 * TLS connection; without TLS the channel is not encrypted, like the TCP connection then.
 */

namespace AnchorUdp {

constexpr size_t HEADER_SIZE = 16;
constexpr size_t TAG_SIZE = 16;
constexpr size_t KEY_SIZE = 16;
// Payloads larger than this go over TCP instead, so a datagram (payload, header, tag, UDP and IPv6 headers) stays
// within the 1500 byte MTU of a normal internet connection and doesn't get split up
constexpr size_t MAX_PAYLOAD = 1400;
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_ENCRYPTED = 1;

enum Direction : uint8_t {
    ClientToServer = 1,
    ServerToClient = 2,
};

class Codec {
  public:
    Codec() = default;
    ~Codec();
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;

    // An empty key means plain datagrams. Returns false for a key this build can't use.
    bool SetKey(const std::vector<uint8_t>& key);
    bool IsEncrypted() const {
        return encrypted;
    }

    // Builds a datagram, false when the payload is too big
    bool Seal(Direction direction, uint32_t token, uint64_t sequence, const std::string& payload,
              std::vector<uint8_t>& datagram) const;
    // Checks and reads a datagram, false when it isn't one of ours or doesn't decrypt
    bool Open(Direction direction, const uint8_t* data, size_t length, uint32_t& token, uint64_t& sequence,
              std::string& payload) const;

  private:
    void* gcm = nullptr; // mbedtls_gcm_context
    bool encrypted = false;
};

// "0a1b..." -> bytes, empty on malformed input
std::vector<uint8_t> HexDecode(const std::string& hex);

} // namespace AnchorUdp

#endif // __cplusplus
