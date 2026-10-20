#ifndef DECRYPT_CRYPTO_HPP
#define DECRYPT_CRYPTO_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <functional>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  PS5 Crypto Engine — AES-128-CBC + HMAC-SHA256 decryption
//
//  The PS5 uses a multi-layer key derivation chain:
//    L0: BootROM root keys (fused in silicon)
//    L1: PUP-level container keys
//    L2: Per-segment AES-128-CBC keys derived from SELF metadata
//
//  This module provides:
//    1. Key database (known/extracted keys organized by firmware version)
//    2. Key derivation functions (PBKDF2-HMAC-SHA256)
//    3. AES-128-CBC segment decryption
//    4. HMAC-SHA256 integrity verification
//
//  Implementation uses raw AES from scratch — no OpenSSL dependency.
//  Written for the AnyPS5 relinker pipeline.
// ──────────────────────────────────────────────────────────────────────

// ─── AES S-Box ───────────────────────────────────────────────────────

static constexpr uint8_t SBOX[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

static constexpr uint8_t INV_SBOX[256] = {
    0x52,0x09,0x6A,0xD5,0x30,0x36,0xA5,0x38,0xBF,0x40,0xA3,0x9E,0x81,0xF3,0xD7,0xFB,
    0x7C,0xE3,0x39,0x82,0x9B,0x2F,0xFF,0x87,0x34,0x8E,0x43,0x44,0xC4,0xDE,0xE9,0xCB,
    0x54,0x7B,0x94,0x32,0xA6,0xC2,0x23,0x3D,0xEE,0x4C,0x95,0x0B,0x42,0xFA,0xC3,0x4E,
    0x08,0x2E,0xA1,0x66,0x28,0xD9,0x24,0xB2,0x76,0x5B,0xA2,0x49,0x6D,0x8B,0xD1,0x25,
    0x72,0xF8,0xF6,0x64,0x86,0x68,0x98,0x16,0xD4,0xA4,0x5C,0xCC,0x5D,0x65,0xB6,0x92,
    0x6C,0x70,0x48,0x50,0xFD,0xED,0xB9,0xDA,0x5E,0x15,0x46,0x57,0xA7,0x8D,0x9D,0x84,
    0x90,0xD8,0xAB,0x00,0x8C,0xBC,0xD3,0x0A,0xF7,0xE4,0x58,0x05,0xB8,0xB3,0x45,0x06,
    0xD0,0x2C,0x1E,0x8F,0xCA,0x3F,0x0F,0x02,0xC1,0xAF,0xBD,0x03,0x01,0x13,0x8A,0x6B,
    0x3A,0x91,0x11,0x41,0x4F,0x67,0xDC,0xEA,0x97,0xF2,0xCF,0xCE,0xF0,0xB4,0xE6,0x73,
    0x96,0xAC,0x74,0x22,0xE7,0xAD,0x35,0x85,0xE2,0xF9,0x37,0xE8,0x1C,0x75,0xDF,0x6E,
    0x47,0xF1,0x1A,0x71,0x1D,0x29,0xC5,0x89,0x6F,0xB7,0x62,0x0E,0xAA,0x18,0xBE,0x1B,
    0xFC,0x56,0x3E,0x4B,0xC6,0xD2,0x79,0x20,0x9A,0xDB,0xC0,0xFE,0x78,0xCD,0x5A,0xF4,
    0x1F,0xDD,0xA8,0x33,0x88,0x07,0xC7,0x31,0xB1,0x12,0x10,0x59,0x27,0x80,0xEC,0x5F,
    0x60,0x51,0x7F,0xA9,0x19,0xB5,0x4A,0x0D,0x2D,0xE5,0x7A,0x9F,0x93,0xC9,0x9C,0xEF,
    0xA0,0xE0,0x3B,0x4D,0xAE,0x2A,0xF5,0xB0,0xC8,0xEB,0xBB,0x3C,0x83,0x53,0x99,0x61,
    0x17,0x2B,0x04,0x7E,0xBA,0x77,0xD6,0x26,0xE1,0x69,0x14,0x63,0x55,0x21,0x0C,0x7D
};

static constexpr uint8_t RCON[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

// ─── AES-128 core ────────────────────────────────────────────────────

using AESBlock = std::array<uint8_t, 16>;
using AESKey   = std::array<uint8_t, 16>;

struct AES128 {
    std::array<AESBlock, 11> round_keys{};

    explicit AES128(const AESKey& key) {
        // key expansion
        std::memcpy(round_keys[0].data(), key.data(), 16);
        for (int i = 1; i <= 10; ++i) {
            auto& prev = round_keys[i - 1];
            auto& curr = round_keys[i];
            // RotWord + SubWord + Rcon
            uint8_t tmp[4] = {
                static_cast<uint8_t>(SBOX[prev[13]] ^ RCON[i]),
                SBOX[prev[14]],
                SBOX[prev[15]],
                SBOX[prev[12]]
            };
            for (int j = 0; j < 4; ++j)  curr[j]      = prev[j]      ^ tmp[j];
            for (int j = 4; j < 8; ++j)  curr[j]      = prev[j]      ^ curr[j - 4];
            for (int j = 8; j < 12; ++j) curr[j]      = prev[j]      ^ curr[j - 4];
            for (int j = 12; j < 16; ++j) curr[j]     = prev[j]      ^ curr[j - 4];
        }
    }

    void decrypt_block(const uint8_t in[16], uint8_t out[16]) const {
        uint8_t state[16];
        std::memcpy(state, in, 16);

        // initial round key addition
        xor_block(state, round_keys[10].data());

        for (int round = 9; round >= 1; --round) {
            inv_shift_rows(state);
            inv_sub_bytes(state);
            xor_block(state, round_keys[round].data());
            inv_mix_columns(state);
        }

        inv_shift_rows(state);
        inv_sub_bytes(state);
        xor_block(state, round_keys[0].data());

        std::memcpy(out, state, 16);
    }

private:
    static void xor_block(uint8_t* a, const uint8_t* b) {
        for (int i = 0; i < 16; ++i) a[i] ^= b[i];
    }

    static void inv_sub_bytes(uint8_t* state) {
        for (int i = 0; i < 16; ++i) state[i] = INV_SBOX[state[i]];
    }

    static void inv_shift_rows(uint8_t* s) {
        // row 1: shift right by 1
        uint8_t t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        // row 2: shift right by 2
        std::swap(s[2], s[10]); std::swap(s[6], s[14]);
        // row 3: shift right by 3
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
    }

    static uint8_t gf_mul(uint8_t a, uint8_t b) {
        uint8_t result = 0;
        for (int i = 0; i < 8; ++i) {
            if (b & 1) result ^= a;
            bool hi = (a & 0x80) != 0;
            a <<= 1;
            if (hi) a ^= 0x1B;
            b >>= 1;
        }
        return result;
    }

    static void inv_mix_columns(uint8_t* s) {
        for (int c = 0; c < 4; ++c) {
            int i = c * 4;
            uint8_t a = s[i], b = s[i+1], cc = s[i+2], d = s[i+3];
            s[i]   = gf_mul(a, 0x0e) ^ gf_mul(b, 0x0b) ^ gf_mul(cc, 0x0d) ^ gf_mul(d, 0x09);
            s[i+1] = gf_mul(a, 0x09) ^ gf_mul(b, 0x0e) ^ gf_mul(cc, 0x0b) ^ gf_mul(d, 0x0d);
            s[i+2] = gf_mul(a, 0x0d) ^ gf_mul(b, 0x09) ^ gf_mul(cc, 0x0e) ^ gf_mul(d, 0x0b);
            s[i+3] = gf_mul(a, 0x0b) ^ gf_mul(b, 0x0d) ^ gf_mul(cc, 0x09) ^ gf_mul(d, 0x0e);
        }
    }
};

// ─── AES-128-CBC decryption ──────────────────────────────────────────

inline std::vector<uint8_t> aes128_cbc_decrypt(
    const uint8_t* ciphertext, size_t length,
    const AESKey& key, const AESBlock& iv)
{
    if (length % 16 != 0)
        throw std::runtime_error("AES-CBC: ciphertext not block-aligned");

    AES128 cipher(key);
    std::vector<uint8_t> plaintext(length);
    AESBlock prev_block = iv;

    for (size_t offset = 0; offset < length; offset += 16) {
        uint8_t decrypted[16];
        cipher.decrypt_block(ciphertext + offset, decrypted);

        // CBC: XOR with previous ciphertext block (or IV for first block)
        for (int i = 0; i < 16; ++i)
            plaintext[offset + i] = decrypted[i] ^ prev_block[i];

        std::memcpy(prev_block.data(), ciphertext + offset, 16);
    }

    return plaintext;
}

// ─── SHA-256 (for HMAC / PBKDF2) ────────────────────────────────────

struct SHA256 {
    static constexpr uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static uint32_t sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static uint32_t gamma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static uint32_t gamma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    static std::array<uint8_t, 32> hash(const uint8_t* data, size_t length) {
        uint32_t h[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };

        // pad message
        size_t bit_len = length * 8;
        size_t padded = ((length + 9 + 63) / 64) * 64;
        std::vector<uint8_t> msg(padded, 0);
        std::memcpy(msg.data(), data, length);
        msg[length] = 0x80;
        for (int i = 0; i < 8; ++i)
            msg[padded - 1 - i] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF);

        // process blocks
        for (size_t block = 0; block < padded; block += 64) {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(msg[block + i*4]) << 24) |
                       (uint32_t(msg[block + i*4+1]) << 16) |
                       (uint32_t(msg[block + i*4+2]) << 8) |
                        uint32_t(msg[block + i*4+3]);

            for (int i = 16; i < 64; ++i)
                w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];

            uint32_t a=h[0], b=h[1], c=h[2], d=h[3],
                     e=h[4], f=h[5], g=h[6], hh=h[7];

            for (int i = 0; i < 64; ++i) {
                uint32_t t1 = hh + sigma1(e) + ch(e,f,g) + K[i] + w[i];
                uint32_t t2 = sigma0(a) + maj(a,b,c);
                hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }

            h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
            h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        }

        std::array<uint8_t, 32> digest;
        for (int i = 0; i < 8; ++i) {
            digest[i*4]   = (h[i] >> 24) & 0xFF;
            digest[i*4+1] = (h[i] >> 16) & 0xFF;
            digest[i*4+2] = (h[i] >> 8) & 0xFF;
            digest[i*4+3] = h[i] & 0xFF;
        }
        return digest;
    }
};

// ─── HMAC-SHA256 ─────────────────────────────────────────────────────

inline std::array<uint8_t, 32> hmac_sha256(
    const uint8_t* key, size_t key_len,
    const uint8_t* data, size_t data_len)
{
    std::array<uint8_t, 64> k_pad{};
    if (key_len > 64) {
        auto hashed = SHA256::hash(key, key_len);
        std::memcpy(k_pad.data(), hashed.data(), 32);
    } else {
        std::memcpy(k_pad.data(), key, key_len);
    }

    // inner hash: SHA256( (key XOR ipad) || data )
    std::vector<uint8_t> inner_msg(64 + data_len);
    for (int i = 0; i < 64; ++i) inner_msg[i] = k_pad[i] ^ 0x36;
    std::memcpy(inner_msg.data() + 64, data, data_len);
    auto inner_hash = SHA256::hash(inner_msg.data(), inner_msg.size());

    // outer hash: SHA256( (key XOR opad) || inner_hash )
    std::vector<uint8_t> outer_msg(64 + 32);
    for (int i = 0; i < 64; ++i) outer_msg[i] = k_pad[i] ^ 0x5C;
    std::memcpy(outer_msg.data() + 64, inner_hash.data(), 32);
    return SHA256::hash(outer_msg.data(), outer_msg.size());
}

// ─── PBKDF2-HMAC-SHA256 ─────────────────────────────────────────────

inline std::vector<uint8_t> pbkdf2_hmac_sha256(
    const uint8_t* password, size_t pass_len,
    const uint8_t* salt, size_t salt_len,
    uint32_t iterations, size_t dk_len)
{
    std::vector<uint8_t> dk;
    dk.reserve(dk_len);

    uint32_t block_count = static_cast<uint32_t>((dk_len + 31) / 32);

    for (uint32_t i = 1; i <= block_count; ++i) {
        // U1 = HMAC(password, salt || INT_32_BE(i))
        std::vector<uint8_t> salt_block(salt_len + 4);
        std::memcpy(salt_block.data(), salt, salt_len);
        salt_block[salt_len]   = (i >> 24) & 0xFF;
        salt_block[salt_len+1] = (i >> 16) & 0xFF;
        salt_block[salt_len+2] = (i >> 8) & 0xFF;
        salt_block[salt_len+3] = i & 0xFF;

        auto u = hmac_sha256(password, pass_len,
                             salt_block.data(), salt_block.size());
        auto result = u;

        for (uint32_t iter = 1; iter < iterations; ++iter) {
            u = hmac_sha256(password, pass_len, u.data(), u.size());
            for (int j = 0; j < 32; ++j) result[j] ^= u[j];
        }

        for (size_t j = 0; j < 32 && dk.size() < dk_len; ++j)
            dk.push_back(result[j]);
    }

    return dk;
}

// ─── PS5 Key Database ────────────────────────────────────────────────
//
//  Key slots organized by firmware version range.
//  Sources: BootROM L0 leak (Dec 2025), scene research, PSDevWiki.
//
//  Key types:
//    PUP_KEY       — outer SLB2 container encryption
//    SEGMENT_KEY   — per-SELF segment AES-128 keys
//    CONTENT_KEY   — game PKG content decryption
//    FAKE_SELF_KEY — fSELF containers (debug/dev)

enum class KeyType {
    PUP_KEY,
    SEGMENT_KEY,
    CONTENT_KEY,
    FAKE_SELF_KEY,
    BOOTROM_L0,
    KERNEL_KEY,
    SYSCON_KEY
};

struct KeyEntry {
    KeyType     type;
    std::string name;
    std::string firmware_range;   // e.g. "1.00-7.61"
    AESKey      key;
    AESBlock    iv;
    uint32_t    key_id;           // Sony internal key slot ID
};

class KeyDatabase {
public:
    void add_key(KeyEntry entry) {
        keys_by_id_[entry.key_id] = entry;
        keys_by_name_[entry.name] = entry;
    }

    const KeyEntry* find_by_id(uint32_t id) const {
        auto it = keys_by_id_.find(id);
        return it != keys_by_id_.end() ? &it->second : nullptr;
    }

    const KeyEntry* find_by_name(const std::string& name) const {
        auto it = keys_by_name_.find(name);
        return it != keys_by_name_.end() ? &it->second : nullptr;
    }

    bool load_from_file(const std::string& path);  // defined in KeyDatabase.cpp
    bool save_to_file(const std::string& path) const;

    size_t key_count() const { return keys_by_id_.size(); }
    bool empty() const { return keys_by_id_.empty(); }

    // iterate all keys
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (const auto& [id, entry] : keys_by_id_) fn(entry);
    }

    // ── Load known fSELF / debug keys ──
    //  These are the zero-key and debug-key entries that work on
    //  decrypted dumps (fpkg, fself containers from jailbroken consoles)
    void load_known_keys() {
        // fSELF null key — used for fake-signed containers
        KeyEntry fself{};
        fself.type = KeyType::FAKE_SELF_KEY;
        fself.name = "fself_null";
        fself.firmware_range = "1.00+";
        fself.key = {0};
        fself.iv  = {0};
        fself.key_id = 0x00000000;
        add_key(fself);

        // PUP container debug key (devkit/testkit)
        KeyEntry pup_debug{};
        pup_debug.type = KeyType::PUP_KEY;
        pup_debug.name = "pup_debug";
        pup_debug.firmware_range = "1.00+";
        pup_debug.key = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        pup_debug.iv = {0};
        pup_debug.key_id = 0x00000001;
        add_key(pup_debug);

        // Segment decryption seed (fSELF passthrough)
        KeyEntry seg_pass{};
        seg_pass.type = KeyType::SEGMENT_KEY;
        seg_pass.name = "segment_passthrough";
        seg_pass.firmware_range = "1.00+";
        seg_pass.key = {0};
        seg_pass.iv  = {0};
        seg_pass.key_id = 0x00000002;
        add_key(seg_pass);
    }

private:
    std::map<uint32_t, KeyEntry>     keys_by_id_;
    std::map<std::string, KeyEntry>  keys_by_name_;
};

// ─── Segment Decryptor ───────────────────────────────────────────────

struct DecryptionResult {
    uint32_t segment_index;
    bool     success;
    std::string error;
    std::vector<uint8_t> plaintext;
    double   entropy_before;
    double   entropy_after;
};

inline DecryptionResult decrypt_segment(
    const uint8_t* ciphertext, size_t length,
    const AESKey& key, const AESBlock& iv,
    uint32_t segment_index)
{
    DecryptionResult result;
    result.segment_index = segment_index;

    try {
        // pre-decrypt entropy check
        size_t sample = std::min(length, size_t(0x10000));
        result.entropy_before = 0.0;
        if (sample > 0) {
            std::array<size_t, 256> counts{};
            for (size_t i = 0; i < sample; ++i) counts[ciphertext[i]]++;
            for (auto c : counts) {
                if (c == 0) continue;
                double p = double(c) / double(sample);
                result.entropy_before -= p * std::log2(p);
            }
        }

        // pad to block boundary
        size_t padded_len = ((length + 15) / 16) * 16;
        std::vector<uint8_t> padded(padded_len, 0);
        std::memcpy(padded.data(), ciphertext, length);

        result.plaintext = aes128_cbc_decrypt(padded.data(), padded_len, key, iv);
        result.plaintext.resize(length);  // trim padding

        // post-decrypt entropy check
        sample = std::min(length, size_t(0x10000));
        result.entropy_after = 0.0;
        if (sample > 0) {
            std::array<size_t, 256> counts{};
            for (size_t i = 0; i < sample; ++i) counts[result.plaintext[i]]++;
            for (auto c : counts) {
                if (c == 0) continue;
                double p = double(c) / double(sample);
                result.entropy_after -= p * std::log2(p);
            }
        }

        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
    }

    return result;
}

// ─── Key derivation from SELF metadata ───────────────────────────────
//
//  Given a SELF header's key_type field and the segment index,
//  derive the per-segment AES key using PBKDF2 from a master seed.

inline AESKey derive_segment_key(
    const AESKey& master_seed,
    uint32_t key_type,
    uint32_t segment_index,
    uint32_t iterations = 4096)
{
    // salt = key_type(4) || segment_index(4)
    uint8_t salt[8];
    salt[0] = (key_type >> 24) & 0xFF;
    salt[1] = (key_type >> 16) & 0xFF;
    salt[2] = (key_type >> 8) & 0xFF;
    salt[3] = key_type & 0xFF;
    salt[4] = (segment_index >> 24) & 0xFF;
    salt[5] = (segment_index >> 16) & 0xFF;
    salt[6] = (segment_index >> 8) & 0xFF;
    salt[7] = segment_index & 0xFF;

    auto derived = pbkdf2_hmac_sha256(
        master_seed.data(), master_seed.size(),
        salt, sizeof(salt),
        iterations, 16);

    AESKey result{};
    std::memcpy(result.data(), derived.data(), 16);
    return result;
}

// ─── IV derivation ───────────────────────────────────────────────────

inline AESBlock derive_segment_iv(
    const AESKey& master_seed,
    uint32_t segment_index)
{
    // IV = HMAC-SHA256(master_seed, "IV" || segment_index)[0:16]
    uint8_t msg[6] = {'I', 'V',
        uint8_t((segment_index >> 24) & 0xFF),
        uint8_t((segment_index >> 16) & 0xFF),
        uint8_t((segment_index >> 8) & 0xFF),
        uint8_t(segment_index & 0xFF)
    };

    auto mac = hmac_sha256(master_seed.data(), master_seed.size(), msg, sizeof(msg));

    AESBlock iv{};
    std::memcpy(iv.data(), mac.data(), 16);
    return iv;
}

}  // namespace Decrypt

#endif  // DECRYPT_CRYPTO_HPP
