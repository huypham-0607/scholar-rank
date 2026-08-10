#ifndef CONSTRUCT_DOC_LEN_LIST_H
#define CONSTRUCT_DOC_LEN_LIST_H

#include "scholar_rank/utils/file_io.h"
#include <filesystem>
#include <vector>

void write_doc_len_entry(
    const SafeFile& out_fp,
    const unsigned long long& delta,
    const unsigned char& freq
);

/**
 * @brief Construct doc_id - doc_len sequence (out_dir/doc_len_list.bin).
 *
 * Format: (doc_id<vbe_encoding>)(doc_len<unsigned char>)
 */
void construct_doc_len_list(
    const std::filesystem::path& in_dir,
    const std::filesystem::path& out_dir
);

/**
 * @brief Read doc_len_list.bin (a single file path, not a directory)
 * into a dense doc_id -> doc_len array.
 */
void read_doc_len_list(
    const std::filesystem::path& in_dir,
    std::vector<unsigned char>& doc_len_list
);

#endif
