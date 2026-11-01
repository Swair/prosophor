// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <openssl/evp.h>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace prosophor {

/// Base64Encode - 标准 base64 编码（无换行）
inline std::string Base64Encode(const std::vector<uint8_t>& data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(i + 1 < data.size() ? table[(n >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

/// Sha256Hex - Compute SHA-256 digest and return as uppercase hex string
inline std::string Sha256Hex(const std::string& input) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int len = 0;
    EVP_Digest(input.data(), input.size(), digest.data(), &len, EVP_sha256(), nullptr);
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

/// GenerateUuid - Generate a UUID v4 string (32 hex chars, no dashes)
inline std::string GenerateUuid() {
    std::array<uint8_t, 16> bytes{};
    std::random_device rd;
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = rd();
        std::memcpy(bytes.data() + i, &r, 4);
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

}  // namespace prosophor
