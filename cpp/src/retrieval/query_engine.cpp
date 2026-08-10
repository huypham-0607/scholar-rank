/**
 * @file query_engine.cpp
 * 
 * @brief Query engine for Lexical retrieval
 * 
 */

#include "scholar_rank/retrieval/merge_inverted_blocks.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

void query (
    std::vector<std::string> query,
    fs::path in_path
) {
    std::vector<std::pair<std::string, TermMeta>> raw_term_meta_mapping = read_block_meta_file(
        in_path
    );

    std::unordered_map<std::string, TermMeta> term_meta_mapping;
    for (auto [term,metadata] : raw_term_meta_mapping) {
        term_meta_mapping[term] = metadata;
    }
}