#ifndef MERGE_INVERTED_BLOCKS_H
#define MERGE_INVERTED_BLOCKS_H

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Per-block BMW metadata.
 *
 * doc_id is this block's absolute start_doc_id - binary-searchable
 * directly, no reconstruction needed by callers. On disk, consecutive
 * blocks' start_doc_id are still delta+VBE encoded for space; the
 * delta<->absolute conversion happens at the serialization boundary
 * (write_block_meta_file encodes the delta, read_block_meta_file
 * reconstructs the absolute value), so every BlockMeta that ever exists in
 * memory - just-flushed or just-loaded - already holds an absolute doc_id.
 */
struct BlockMeta {
    unsigned long long doc_id;
    unsigned int file_index;
    size_t start_addr;
    float block_ub;

    BlockMeta(
        unsigned long long _doc_id,
        unsigned int _file_index,
        size_t _start_addr,
        float _block_ub
    );
};

/**
 * @brief Per-term BMW metadata: the term-level upper bound plus the list
 * of this term's blocks.
 */
struct TermMeta {
    float term_ub;
    unsigned long long max_doc_id;
    unsigned int doc_count;
    std::vector<BlockMeta> block_meta_list;

    TermMeta();
};

/**
 * @brief Merge partial SPIMI blocks (construct_inverted_blocks output) into
 * a small, fixed number of final posting-data files plus one consolidated
 * BMW block-metadata file (out_dir / "block_meta.bin").
 *
 * @param doc_len_dir path to doc_len_list.bin (construct_doc_len_list output)
 * @param in_dir directory containing partial block_*.bin files
 * @param out_dir directory to write posting_*.bin + block_meta.bin into
 * @param k1 BM25 k1 parameter
 * @param b BM25 b parameter
 * @param block_size number of postings per BMW block
 * @param split_size approximate max bytes per posting_*.bin output file
 */
void merge_inverted_blocks(
    const std::filesystem::path& doc_len_dir,
    const std::filesystem::path& in_dir,
    const std::filesystem::path& out_dir,
    const float k1 = 1.2f,
    const float b = 0.75f,
    const int block_size = 128,
    const size_t split_size = (1ull << 30)
);

std::vector<std::pair<std::string, TermMeta>> read_block_meta_file(
    const std::filesystem::path& in_path
);

#endif
