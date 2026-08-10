/**
 * @file build_doc_len_list.cpp
 * @brief CLI entry point: document length list construction.
 */

#include "scholar_rank/retrieval/construct_doc_len_list.h"
#include "scholar_rank/utils/logger.h"

#include <filesystem>
#include <format>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <in_dir> <out_dir>\n";
        return 1;
    }

    Logger logger(__FILE_NAME__, Logger::INFO);

    try {
        fs::path in_dir = argv[1];
        fs::path out_dir = argv[2];

        fs::create_directories(out_dir);

        logger.log(std::format(
            "Building document length list from {} into {}.",
            in_dir.string(), out_dir.string()
        ));

        construct_doc_len_list(in_dir, out_dir);

        logger.log("Finished building document length list.");
    } catch (const std::exception& e) {
        logger.log(std::format("Failed: {}", e.what()), Logger::ERROR);
        return 1;
    }

    return 0;
}
