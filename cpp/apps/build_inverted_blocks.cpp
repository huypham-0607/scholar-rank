/**
 * @file build_inverted_blocks.cpp
 * @brief CLI entry point: SPIMI partial-block construction (construct_inverted_blocks)
 * over tokenized input, ahead of merge_inverted_blocks.
 */

#include "scholar_rank/retrieval/index_builder.h"
#include "scholar_rank/utils/logger.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <in_dir> <out_dir> <mem_limit_bytes>\n";
        return 1;
    }

    Logger logger(__FILE_NAME__, Logger::INFO);

    try {
        fs::path in_dir = argv[1];
        fs::path out_dir = argv[2];
        size_t mem_limit = std::stoull(argv[3]);

        fs::create_directories(out_dir);

        logger.log(std::format(
            "Building inverted blocks from {} into {} (mem_limit={} bytes).",
            in_dir.string(), out_dir.string(), mem_limit
        ));

        construct_inverted_blocks(in_dir, out_dir, mem_limit);

        logger.log("Finished building inverted blocks.");
    } catch (const std::exception& e) {
        logger.log(std::format("Failed: {}", e.what()), Logger::ERROR);
        return 1;
    }

    return 0;
}
