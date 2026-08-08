#include "consumer/BENCH_Handler.h"
#include "common/util.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstring>

namespace pc {

static std::vector<uint8_t> base64_decode(const std::string& input) {
    uint8_t table[256];
    std::memset(table, 0, sizeof(table));
    table['+'] = 62; table['/'] = 63;
    for (int i = 0; i < 10; i++) table['0' + i] = 52 + i;
    for (int i = 0; i < 26; i++) table['A' + i] = i;
    for (int i = 0; i < 26; i++) table['a' + i] = 26 + i;

    std::vector<uint8_t> output;
    size_t i = 0;
    size_t len = input.size();

    while (i < len) {
        uint32_t o = 0;
        if (i + 3 < len && input[i + 3] != '=') {
            o = (table[static_cast<unsigned char>(input[i])] << 18) |
                (table[static_cast<unsigned char>(input[i + 1])] << 12) |
                (table[static_cast<unsigned char>(input[i + 2])] << 6) |
                (table[static_cast<unsigned char>(input[i + 3])]);
            output.push_back((o >> 16) & 0xFF);
            output.push_back((o >> 8) & 0xFF);
            output.push_back(o & 0xFF);
            i += 4;
        } else if (i + 2 < len && input[i + 2] != '=') {
            o = (table[static_cast<unsigned char>(input[i])] << 18) |
                (table[static_cast<unsigned char>(input[i + 1])] << 12) |
                (table[static_cast<unsigned char>(input[i + 2])] << 6);
            output.push_back((o >> 16) & 0xFF);
            output.push_back((o >> 8) & 0xFF);
            i += 4;
        } else if (i + 1 < len && input[i + 1] != '=') {
            o = (table[static_cast<unsigned char>(input[i])] << 18) |
                (table[static_cast<unsigned char>(input[i + 1])] << 12);
            output.push_back((o >> 16) & 0xFF);
            i += 4;
        } else {
            i++;
        }
    }

    return output;
}

std::string BENCH_Handler::type() const {
    return "BENCH";
}

ResultMessage BENCH_Handler::handle(const WorkUnitMessage& work) {
    ResultMessage result;
    result.work_unit_id = work.work_unit_id;
    result.seq = work.seq;
    result.timestamp = "";

    auto start = std::chrono::steady_clock::now();

    try {
        int64_t offset = work.job.value("offset", 0);
        int64_t chunk_size = work.job.value("chunk_size", 128);
        std::string data_b64 = work.job.value("data", "");
        std::string expected_hash = work.job.value("hash", "");

        std::vector<uint8_t> expected = base64_decode(data_b64);

        std::string local_path = "";
        std::vector<uint8_t> local_chunk;

        if (!work.source_file.empty()) {
            auto filename = std::filesystem::path(work.source_file).filename().string();
            if (std::filesystem::exists(filename)) {
                local_path = filename;
            } else {
                local_path = "./" + filename;
            }

            if (std::filesystem::exists(local_path)) {
                std::ifstream file(local_path, std::ios::binary);
                if (file.is_open()) {
                    file.seekg(offset, std::ios::beg);
                    local_chunk.resize(static_cast<size_t>(chunk_size));
                    file.read(reinterpret_cast<char*>(local_chunk.data()), chunk_size);
                    file.close();
                }
            }
        }

        bool match = false;
        std::string actual_hash = "";

        if (!local_chunk.empty()) {
            actual_hash = sha256_bytes(local_chunk.data(), local_chunk.size());
            match = (actual_hash == expected_hash);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        result.status = "success";
        result.result = nlohmann::json::object();
        result.result["match"] = match;
        result.result["offset"] = offset;
        result.result["chunk_size"] = chunk_size;
        result.result["expected_hash"] = expected_hash;
        result.result["actual_hash"] = actual_hash;
        result.result["duration_ms"] = duration;

        if (!match) {
            std::cerr << "[BENCH_Handler] Chunk mismatch at offset " << offset << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[BENCH_Handler] Processing failed for " << work.work_unit_id
                  << ": " << e.what() << "\n";
        result.status = "failure";
        result.result = nlohmann::json::object();
        result.result["error"] = e.what();
    }

    return result;
}

} // namespace pc
