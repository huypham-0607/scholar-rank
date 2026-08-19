#ifndef CONSTRUCT_INVERTED_BLOCKS_H
#define CONSTRUCT_INVERTED_BLOCKS_H

#include "startorch/retrieval/posting_list.h"
#include "startorch/utils/file_io.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

bool build_partial_index(
    const SafeFile& token_stream,
    const size_t mem_limit,
    std::unordered_map<std::string, PostingList> &posting_list_mapping,
    std::vector<std::string> &dictionary
);

void write_partial_index(
    const std::filesystem::path out_file_path,
    std::unordered_map<std::string, PostingList>& posting_list_mapping,
    std::vector<std::string>& dictionary
);

/**
 * @brief SPIMI: chunk tokenized input (in_dir/token_*.bin) into partial,
 * per-shard inverted blocks (out_dir/block_*.bin), each built under
 * mem_limit bytes.
 */
void construct_inverted_blocks(
    const std::filesystem::path& in_dir,
    const std::filesystem::path& out_dir,
    const size_t mem_limit
);

#endif
