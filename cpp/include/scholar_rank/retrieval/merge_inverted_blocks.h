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
    size_t start_addr;
    float block_ub;

    BlockMeta(
        unsigned long long _doc_id,
        size_t _start_addr,
        float _block_ub
    );
};

/**
 * @brief Per-term BMW metadata: the term-level upper bound plus the list
 * of this term's blocks.
 *
 * file_index lives here, not on BlockMeta: build_posting_list writes one
 * term's entire posting list in a single call, so every block it produces
 * for that term always lands in whichever posting_*.bin file was open at
 * the time - a term's blocks never span multiple files.
 *
 * end_addr is the exclusive end of this term's byte range in that file:
 * every (doc_id, freq) posting for this term lies in
 * [block_meta_list[0].start_addr, end_addr) - a strict half-open interval,
 * so end_addr itself is one past the last valid byte. Combined with the
 * first block's start_addr, this gives a hard, self-contained bound for
 * safe reads/seeks over this term's data without depending on adjacent
 * terms' placement.
 */
struct TermMeta {
    float term_ub;
    size_t end_addr;
    unsigned int doc_count;
    unsigned int file_index;
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

/**
 * @brief Write index build metadata (posting_dir, doc_len_dir, k1, b,
 * block_size, split_size) in two forms: a plain-text key=value file at
 * out_path, one field per line - for humans to read/inspect, never read
 * back programmatically - and a binary twin at out_path with its extension
 * replaced by ".bin" - the exact, length-prefixed-string + fixed-width-field
 * encoding read_metadata actually parses.
 *
 *
 * @param out_path path to write the human-readable metadata file to; the
 * binary file is written alongside it with the same stem and a .bin extension
 * @param posting_dir directory containing this index's posting_*.bin files
 * @param doc_len_dir directory containing this index's doc_len_list.bin
 * @param k1 BM25 k1 parameter the index was built with
 * @param b BM25 b parameter the index was built with
 * @param block_size BMW block size the index was built with
 * @param split_size posting-file split threshold the index was built with
 */
void write_metadata(
    const std::filesystem::path& out_path,
    const std::filesystem::path& posting_dir,
    const std::filesystem::path& doc_len_dir,
    const float k1,
    const float b,
    const int block_size,
    const size_t split_size
);

/**
 * @brief Read back the binary metadata file written by write_metadata (the
 * .bin twin, not the human-readable .txt), populating every out-parameter.
 * Throws std::runtime_error on a missing/unreadable or truncated file.
 *
 * @param in_path path to the .bin metadata file to read
 */
void read_metadata(
    const std::filesystem::path& in_path,
    std::filesystem::path& posting_dir,
    std::filesystem::path& doc_len_dir,
    float& k1,
    float& b,
    int& block_size,
    size_t& split_size
);

#endif
