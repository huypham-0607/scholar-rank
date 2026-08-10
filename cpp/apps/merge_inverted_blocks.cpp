/**
 * @file merge_inverted_blocks.cpp
 * @brief CLI entry point: SPIMI partial-block merge (merge_inverted_blocks). Construct full
 * posting list & block metadata for corpus.
 */

#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/logger.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {

    std::string usage_err = std::format(
        "Usage: {} <doc_len_dir> <in_dir> <out_dir> <optional_flags>\n"
        "Optional flags:\n"
        "k1=(float)                 - k1 parameter in BM25. [1,2]\n"
        "b=(float)                  - b parameter in BM25. [0,1]\n"
        "block_size=(unsigned int)  - Block partition size for Block-Max WAND.\n"
        "split_size=(size_t)  - Splitting threshold for posting list serialization.",
        argv[0]
    );
    
    if (argc < 4 || argc > 8) {
        std::cerr << usage_err << "\n";
        return 1;
    }

    fs::path doc_len_dir = argv[1];
    fs::path in_dir = argv[2];
    fs::path out_dir = argv[3];

    float k1 = 1.2f, b = 0.75f;
    int block_size = 128;
    size_t split_size = 1ull << 30;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        if (eq == std::string::npos) {
            std::cerr << usage_err << "\n";
            return 1;
        }
        std::string key = arg.substr(0, eq), val = arg.substr(eq + 1);
        if (key == "k1") {
            try {
                k1 = std::stof(val);
            }
            catch (const std::exception& e) {
                std::cerr << std::format("Cannot convert value {} to type 'float' for flag {}", val, key) << "\n";
                return 1;
            }
            if (k1 < 1 || 2 < k1) {
                std::cerr << std::format("Value {} for flag {} out of range [1,2]", k1, key) << "\n";
                return 1;  
            }
        }
        else if (key == "b") {
            try {
                b = std::stof(val);
            }
            catch (const std::exception& e) {
                std::cerr << std::format("Cannot convert value {} to type 'float' for flag {}", val, key) << "\n";
                return 1;
            }
            if (b < 0 || 1 < b) {
                std::cerr << std::format("Value {} for flag {} out of range [0,1]", b, key) << "\n";
                return 1;  
            }
        }
        else if (key == "block_size") {
            try {
                block_size = std::stoi(val);
            }
            catch (const std::exception& e) {
                std::cerr << std::format("Cannot convert value {} to type 'int' for flag {}", val, key) << "\n";
                return 1;
            }
        }
        else if (key == "split_size") {
            try {
                split_size = std::stoull(val);
            }
            catch (const std::exception& e) {
                std::cerr << std::format("Cannot convert value {} to type 'size_t' for flag {}", val, key) << "\n";
                return 1;
            }
        }
        else {
            std::cerr << std::format("Unknown flag {}.", key) << "\n";
            return 1;
        }
    }

    Logger logger(__FILE_NAME__, Logger::INFO);

    try {
        fs::create_directories(out_dir);

        logger.log(std::format(
            "Merging inverted blocks from {} into {}, using doc length list at {}.\n"
            "Params:\n"
            "k1 = {}\n"
            "b = {}\n"
            "block_size = {}\n"
            "split_size = {}",
            in_dir.string(),
            out_dir.string(),
            doc_len_dir.string(),
            k1,
            b,
            block_size,
            split_size
        ));

        merge_inverted_blocks(
            doc_len_dir,
            in_dir,
            out_dir,
            k1,
            b,
            block_size,
            split_size
        );

        logger.log("Finished merging inverted blocks.");
    } catch (const std::exception& e) {
        logger.log(std::format("Failed: {}", e.what()), Logger::ERROR);
        return 1;
    }

    return 0;
}
