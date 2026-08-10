#ifndef MERGE_INVERTED_BLOCKS_H
#define MERGE_INVERTED_BLOCKS_H

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Per-block BMW metadata.
 *
 * delta is the difference between this block's start_doc_id and the
 * previous block's start_doc_id for the same term (0 => absolute doc_id,
 * for a term's first block). This is what's written to disk (VBE encoded).
 * Reconstructing absolute start_doc_id per block (required before it's
 * binary-searchable) is a loader-side responsibility: accumulate delta
 * across a term's block_meta_list in order.
 */
struct BlockMeta {
    unsigned long long delta;
    unsigned int file_index;
    size_t start_addr;
    float block_ub;

    BlockMeta(
        unsigned long long _delta,
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
