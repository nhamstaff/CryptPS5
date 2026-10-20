#include <decrypt/Crypto.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Decrypt {

// ── Key file format ──
//
//  Simple text format, one key per line:
//    <key_id_hex> <type> <name> <firmware_range> <key_hex_32> <iv_hex_32>
//
//  Example:
//    00000010 SEGMENT_KEY kernel_seg_0 1.00-7.61 0123456789ABCDEF0123456789ABCDEF 00000000000000000000000000000000
//
//  Lines starting with # are comments.
//  Type values: PUP_KEY, SEGMENT_KEY, CONTENT_KEY, FAKE_SELF_KEY,
//               BOOTROM_L0, KERNEL_KEY, SYSCON_KEY

static uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static bool parse_hex_bytes(const std::string& hex, uint8_t* out, size_t expected) {
    if (hex.size() != expected * 2) return false;
    for (size_t i = 0; i < expected; ++i) {
        out[i] = (hex_nibble(hex[i*2]) << 4) | hex_nibble(hex[i*2+1]);
    }
    return true;
}

static std::string bytes_to_hex(const uint8_t* data, size_t len) {
    const char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 0x0F];
    }
    return result;
}

static KeyType parse_key_type(const std::string& s) {
    if (s == "PUP_KEY")       return KeyType::PUP_KEY;
    if (s == "SEGMENT_KEY")   return KeyType::SEGMENT_KEY;
    if (s == "CONTENT_KEY")   return KeyType::CONTENT_KEY;
    if (s == "FAKE_SELF_KEY") return KeyType::FAKE_SELF_KEY;
    if (s == "BOOTROM_L0")    return KeyType::BOOTROM_L0;
    if (s == "KERNEL_KEY")    return KeyType::KERNEL_KEY;
    if (s == "SYSCON_KEY")    return KeyType::SYSCON_KEY;
    return KeyType::PUP_KEY;
}

static const char* key_type_string(KeyType type) {
    switch (type) {
        case KeyType::PUP_KEY:       return "PUP_KEY";
        case KeyType::SEGMENT_KEY:   return "SEGMENT_KEY";
        case KeyType::CONTENT_KEY:   return "CONTENT_KEY";
        case KeyType::FAKE_SELF_KEY: return "FAKE_SELF_KEY";
        case KeyType::BOOTROM_L0:    return "BOOTROM_L0";
        case KeyType::KERNEL_KEY:    return "KERNEL_KEY";
        case KeyType::SYSCON_KEY:    return "SYSCON_KEY";
    }
    return "UNKNOWN";
}

bool KeyDatabase::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;

    std::string line;
    int loaded = 0;

    while (std::getline(file, line)) {
        // trim whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.erase(line.begin());
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string id_hex, type_str, name, fw_range, key_hex, iv_hex;
        iss >> id_hex >> type_str >> name >> fw_range >> key_hex >> iv_hex;

        if (id_hex.empty() || key_hex.empty()) continue;

        KeyEntry entry{};
        entry.key_id = static_cast<uint32_t>(std::stoul(id_hex, nullptr, 16));
        entry.type = parse_key_type(type_str);
        entry.name = name;
        entry.firmware_range = fw_range;

        if (!parse_hex_bytes(key_hex, entry.key.data(), 16)) continue;
        if (!iv_hex.empty()) {
            parse_hex_bytes(iv_hex, entry.iv.data(), 16);
        }

        add_key(entry);
        loaded++;
    }

    return loaded > 0;
}

bool KeyDatabase::save_to_file(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return false;

    file << "# PS5 Key Database — AnyPS5 Decryptor\n"
         << "# Format: <key_id_hex> <type> <name> <firmware_range> <key_hex> <iv_hex>\n"
         << "#\n";

    for_each([&](const KeyEntry& entry) {
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%08X", entry.key_id);

        file << id_buf << " "
             << key_type_string(entry.type) << " "
             << entry.name << " "
             << entry.firmware_range << " "
             << bytes_to_hex(entry.key.data(), 16) << " "
             << bytes_to_hex(entry.iv.data(), 16) << "\n";
    });

    return true;
}

}  // namespace Decrypt
