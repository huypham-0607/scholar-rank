/**
 * @file query_engine.cpp
 * 
 * @brief Query engine for Lexical retrieval
 * 
 */

#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/file_io.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

void find_pivot() {

}

void move_posting() {

}

void move_posting_shallow() {

}

void full_evaluate() {

}

void get_new_candidate() {

}

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

    std::unordered_map<unsigned int, SafeFileMmap> file_index_mapping;
   
}

class PostingPointer {
    PostingPointer(
        std::string term,
        const std::unordered_map<std::string, TermMeta> &term_meta_mapping,
        const std::unordered_map<unsigned int, SafeFileMmap> &file_Index_mapping
    ) {

    }
};