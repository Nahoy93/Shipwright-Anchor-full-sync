#include "UdpChannel.h"

#include <cstring>

#ifdef ENABLE_ANCHOR_TLS
#include <mbedtls/gcm.h>
#endif

namespace AnchorUdp {

static void PutU32(uint8_t* out, uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out[i] = (uint8_t)(value >> (8 * i));
    }
}

static void PutU64(uint8_t* out, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint32_t GetU32(const uint8_t* in) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        value |= (uint32_t)in[i] << (8 * i);
    }
    return value;
}

static uint64_t GetU64(const uint8_t* in) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (uint64_t)in[i] << (8 * i);
    }
    return value;
}

static void MakeNonce(Direction direction, uint64_t sequence, uint8_t nonce[12]) {
    memset(nonce, 0, 12);
    nonce[0] = direction;
    PutU64(nonce + 4, sequence);
}

Codec::~Codec() {
#ifdef ENABLE_ANCHOR_TLS
    if (gcm != nullptr) {
        mbedtls_gcm_free((mbedtls_gcm_context*)gcm);
        delete (mbedtls_gcm_context*)gcm;
    }
#endif
}

bool Codec::SetKey(const std::vector<uint8_t>& key) {
    encrypted = false;
    if (key.empty()) {
        return true;
    }
#ifdef ENABLE_ANCHOR_TLS
    if (key.size() != KEY_SIZE) {
        return false;
    }
    if (gcm == nullptr) {
        gcm = new mbedtls_gcm_context;
        mbedtls_gcm_init((mbedtls_gcm_context*)gcm);
    }
    if (mbedtls_gcm_setkey((mbedtls_gcm_context*)gcm, MBEDTLS_CIPHER_ID_AES, key.data(),
                           (unsigned int)key.size() * 8) != 0) {
        return false;
    }
    encrypted = true;
    return true;
#else
    return false;
#endif
}

bool Codec::Seal(Direction direction, uint32_t token, uint64_t sequence, const std::string& payload,
                 std::vector<uint8_t>& datagram) const {
    if (payload.size() > MAX_PAYLOAD) {
        return false;
    }

    datagram.resize(HEADER_SIZE + payload.size() + (encrypted ? TAG_SIZE : 0));
    uint8_t* header = datagram.data();
    header[0] = 'A';
    header[1] = 'U';
    header[2] = VERSION;
    header[3] = encrypted ? FLAG_ENCRYPTED : 0;
    PutU32(header + 4, token);
    PutU64(header + 8, sequence);

    if (!encrypted) {
        memcpy(datagram.data() + HEADER_SIZE, payload.data(), payload.size());
        return true;
    }

#ifdef ENABLE_ANCHOR_TLS
    uint8_t nonce[12];
    MakeNonce(direction, sequence, nonce);
    uint8_t* body = datagram.data() + HEADER_SIZE;
    uint8_t* tag = body + payload.size();
    return mbedtls_gcm_crypt_and_tag((mbedtls_gcm_context*)gcm, MBEDTLS_GCM_ENCRYPT, payload.size(), nonce,
                                     sizeof(nonce), header, HEADER_SIZE, (const uint8_t*)payload.data(), body, TAG_SIZE,
                                     tag) == 0;
#else
    return false;
#endif
}

bool Codec::Open(Direction direction, const uint8_t* data, size_t length, uint32_t& token, uint64_t& sequence,
                 std::string& payload) const {
    if (length < HEADER_SIZE || data[0] != 'A' || data[1] != 'U' || data[2] != VERSION) {
        return false;
    }

    bool isEncrypted = (data[3] & FLAG_ENCRYPTED) != 0;
    // Never accept plain datagrams once the channel is encrypted (and the other way around)
    if (isEncrypted != encrypted) {
        return false;
    }

    token = GetU32(data + 4);
    sequence = GetU64(data + 8);

    if (!isEncrypted) {
        payload.assign((const char*)data + HEADER_SIZE, length - HEADER_SIZE);
        return true;
    }

#ifdef ENABLE_ANCHOR_TLS
    if (length < HEADER_SIZE + TAG_SIZE) {
        return false;
    }
    size_t bodyLength = length - HEADER_SIZE - TAG_SIZE;
    uint8_t nonce[12];
    MakeNonce(direction, sequence, nonce);
    payload.resize(bodyLength);
    return mbedtls_gcm_auth_decrypt((mbedtls_gcm_context*)gcm, bodyLength, nonce, sizeof(nonce), data, HEADER_SIZE,
                                    data + HEADER_SIZE + bodyLength, TAG_SIZE, data + HEADER_SIZE,
                                    (uint8_t*)payload.data()) == 0;
#else
    return false;
#endif
}

std::vector<uint8_t> HexDecode(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) {
        return bytes;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int high = nibble(hex[i]);
        int low = nibble(hex[i + 1]);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back((uint8_t)((high << 4) | low));
    }
    return bytes;
}

} // namespace AnchorUdp
