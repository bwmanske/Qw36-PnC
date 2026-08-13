#include <gtest/gtest.h>
#include "common/util.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;
using namespace pc;

// ── RFC 6234 Appendix A test vectors ──────────────────────────────

TEST(Sha256, EmptyString) {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string hash = sha256_bytes(nullptr, 0);
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, RFC6234_abc) {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const uint8_t data[] = { 'a', 'b', 'c' };
    std::string hash = sha256_bytes(data, 3);
    EXPECT_EQ(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, RFC6234_abcabc) {
    // SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    // = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    std::string hash = sha256_bytes(
        reinterpret_cast<const uint8_t*>(msg), std::strlen(msg));
    EXPECT_EQ(hash, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, RFC6234_a_repeated_1million) {
    // SHA-256("a" repeated 1,000,000 times)
    // = cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
    std::vector<uint8_t> data(1000000, 'a');
    std::string hash = sha256_bytes(data.data(), data.size());
    EXPECT_EQ(hash, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// ── File hashing ──────────────────────────────────────────────────

TEST(Sha256, FileHash_MatchesBytes) {
    // Create a temp file, hash it, compare with sha256_bytes
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    std::string dir = t ? t : "C:\\Temp";
#else
    std::string dir = "/tmp";
#endif
    std::string path = dir + "/pc_sha256_test_" + std::to_string(
        static_cast<unsigned>(std::hash<std::string>{}(
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) % 100000));

    const char* content = "Hello, SHA-256 test!";
    std::ofstream(path) << content;

    std::string fileHash = sha256_file(path);
    std::string bytesHash = sha256_bytes(
        reinterpret_cast<const uint8_t*>(content), std::strlen(content));

    EXPECT_EQ(fileHash, bytesHash);
    EXPECT_FALSE(fileHash.empty());

    fs::remove(path);
}

TEST(Sha256, FileHash_MissingFile) {
    std::string hash = sha256_file("/nonexistent/path/that/does/not/exist.txt");
    EXPECT_TRUE(hash.empty());
}

TEST(Sha256, FileHash_EmptyFile) {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    std::string dir = t ? t : "C:\\Temp";
#else
    std::string dir = "/tmp";
#endif
    std::string path = dir + "/pc_sha256_empty_" + std::to_string(
        static_cast<unsigned>(std::hash<std::string>{}(
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) % 100000));

    { std::ofstream f(path); } // create empty file

    std::string hash = sha256_file(path);
    // Should match SHA-256 of empty input
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    fs::remove(path);
}

// ── get_data_directory ────────────────────────────────────────────

TEST(Sha256, GetDataDirectory) {
    std::string dir = get_data_directory();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(fs::exists(dir));
    EXPECT_TRUE(fs::is_directory(dir));
}
