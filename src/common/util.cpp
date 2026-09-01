#include "common/util.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cctype>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <sys/resource.h>
#endif

namespace pc {
namespace fs = std::filesystem;

// ── Pure C++ SHA-256 (RFC 6234) ──────────────────────────────────

namespace sha256_impl {

static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t gamma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t gamma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

static void transform(uint32_t state[8], const uint8_t* data) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    for (int i = 16; i < 64; i++) {
        w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // namespace sha256_impl

std::string sha256_bytes(const uint8_t* data, size_t len) {
    using namespace sha256_impl;

    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint8_t block[64];
    size_t offset = 0;
    size_t buffered = 0;

    auto flush_block = [&]() {
        transform(state, block);
        buffered = 0;
    };

    while (offset < len) {
        size_t chunk = std::min(len - offset, sizeof(block) - buffered);
        std::memcpy(block + buffered, data + offset, chunk);
        buffered += chunk;
        offset += chunk;
        if (buffered == sizeof(block)) flush_block();
    }

    // Padding
    uint64_t bitlen = static_cast<uint64_t>(len) * 8;
    block[buffered++] = 0x80;
    if (buffered >= 56) {
        std::memset(block + buffered, 0, 64 - buffered);
        flush_block();
    }
    std::memset(block + buffered, 0, 56 - buffered);
    // Big-endian bit length in last 8 bytes
    for (int i = 7; i >= 0; i--) {
        block[56 + i] = static_cast<uint8_t>(bitlen & 0xFF);
        bitlen >>= 8;
    }
    flush_block();

    std::ostringstream oss;
    for (int i = 0; i < 8; i++) {
        oss << std::hex << std::setfill('0') << std::setw(8) << state[i];
    }
    return oss.str();
}

std::string sha256_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";

    std::vector<uint8_t> buffer(65536);
    uint64_t total = 0;

    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint8_t block[64];
    size_t buffered = 0;

    auto flush_block = [&]() {
        sha256_impl::transform(state, block);
        buffered = 0;
    };

    while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) {
        size_t bytes_read = file.gcount();
        total += bytes_read;

        size_t offset = 0;
        while (offset < bytes_read) {
            size_t chunk = std::min(bytes_read - offset, sizeof(block) - buffered);
            std::memcpy(block + buffered, buffer.data() + offset, chunk);
            buffered += chunk;
            offset += chunk;
            if (buffered == sizeof(block)) flush_block();
        }
    }

    // Handle partial last read
    if (file.gcount() > 0) {
        size_t bytes_read = file.gcount();
        total += bytes_read;

        size_t offset = 0;
        while (offset < bytes_read) {
            size_t chunk = std::min(bytes_read - offset, sizeof(block) - buffered);
            std::memcpy(block + buffered, buffer.data() + offset, chunk);
            buffered += chunk;
            offset += chunk;
            if (buffered == sizeof(block)) flush_block();
        }
    }

    file.close();

    // Padding
    uint64_t bitlen = total * 8;
    block[buffered++] = 0x80;
    if (buffered >= 56) {
        std::memset(block + buffered, 0, 64 - buffered);
        flush_block();
    }
    std::memset(block + buffered, 0, 56 - buffered);
    for (int i = 7; i >= 0; i--) {
        block[56 + i] = static_cast<uint8_t>(bitlen & 0xFF);
        bitlen >>= 8;
    }
    flush_block();

    std::ostringstream oss;
    for (int i = 0; i < 8; i++) {
        oss << std::hex << std::setfill('0') << std::setw(8) << state[i];
    }
    return oss.str();
}

// ── Platform data directory ──────────────────────────────────────

std::string get_data_directory() {
    std::string dir;
#ifdef _WIN32
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata) {
        std::wstring ws(appdata);
        // Convert wide string to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string narrow(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &narrow[0], len, nullptr, nullptr);
        dir = narrow + "\\Producer\\";
    } else {
        dir = "./";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        dir = std::string(home) + "/.local/share/producer/";
    } else {
        dir = "./";
    }
#endif
    fs::create_directories(dir);
    return dir;
}

// ── Duration parsing ─────────────────────────────────────────────

int parse_duration(const std::string& s) {
    if (s.empty()) return 0;
    char suffix = s.back();
    std::string num_part = (suffix == 's' || suffix == 'm' || suffix == 'h')
                            ? s.substr(0, s.size() - 1) : s;
    int value = std::stoi(num_part);
    if (suffix == 'm') return value * 60;
    if (suffix == 'h') return value * 3600;
    return value;
}

// ── Process priority ─────────────────────────────────────────────

bool is_localhost_host(const std::string& host) {
    std::string h = host;
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (h == "localhost" || h == "::1") return true;

    // 127.0.0.0/8 loopback range: "127." followed by exactly three 0-255 octets.
    if (h.rfind("127.", 0) == 0) {
        std::string rest = h.substr(4);
        int octets = 0;
        int cur = 0;
        bool any_digit = false;
        for (char c : rest) {
            if (c >= '0' && c <= '9') {
                cur = cur * 10 + (c - '0');
                any_digit = true;
                if (cur > 255) return false;
            } else if (c == '.') {
                if (!any_digit) return false;
                octets++;
                cur = 0;
                any_digit = false;
            } else {
                return false;
            }
        }
        if (!any_digit) return false;
        octets++; // final octet
        return octets == 3;
    }

    return false;
}

bool set_process_priority_below_normal() {
#ifdef _WIN32
    return SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS) != FALSE;
#else
    // nice +5 approximates Windows BELOW_NORMAL_PRIORITY_CLASS (one step below
    // normal). Raising nice (a positive value) does not require elevated
    // privileges, so this works for unprivileged users.
    static constexpr int kBelowNormalNice = 5;
    return setpriority(PRIO_PROCESS, 0, kBelowNormalNice) == 0;
#endif
}

} // namespace pc
