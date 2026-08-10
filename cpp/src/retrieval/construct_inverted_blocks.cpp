#include "scholar_rank/retrieval/construct_inverted_blocks.h"
#include "scholar_rank/retrieval/posting_list.h"
#include "scholar_rank/retrieval/token_stream.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/vbe.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

constexpr unsigned int EST_UMAP_MEM_PER_ENTRY = 48; // Safe estimation of std::unordered_map mem usage per entry

bool build_partial_index(
    const SafeFile& token_stream,
    const size_t mem_limit,
    std::unordered_map<std::string, PostingList> &posting_list_mapping,
    std::vector<std::string> &dictionary
) {
    size_t mem_usage = 0;

    unsigned long long cur_doc_id;
    std::string cur_term;

    while ((mem_usage < mem_limit/5*4) && read_token(token_stream, &cur_doc_id, &cur_term)) {
        if (posting_list_mapping.find(cur_term) == posting_list_mapping.end()) {
            dictionary.push_back(cur_term);
            posting_list_mapping[cur_term] = PostingList();

            // Estimate memory usage
            mem_usage += sizeof(cur_term) + cur_term.size();
            mem_usage += sizeof(cur_term) + cur_term.size() + sizeof(PostingList) + EST_UMAP_MEM_PER_ENTRY;
        }
        if (!posting_list_mapping[cur_term].has_document(cur_doc_id)) {
            mem_usage += sizeof(PostingItem);
        }
        posting_list_mapping[cur_term].add_document(cur_doc_id);
    }

    std::sort(dictionary.begin(), dictionary.end());
    return (dictionary.size() != 0);
}

/**
 * @brief Write posting list to file.
 *
 * Starts with a dictionary_size <unsigned int>
 * Format for each posting list:
 *
 *       <term_size><posting_list_size><term><<vbe_encoding_{i}><freq_{i}>>
 *
 * - term_size:             unsigned short
 * - posting_list_size:     unsigned int
 * - term:                  char[]
 * - vbe_encoding_{i}:      unsigned char[]
 * - freq:                  unsigned int
 */
void write_partial_index(
    const fs::path out_file_path,
    std::unordered_map<std::string, PostingList>& posting_list_mapping,
    std::vector<std::string>& dictionary
) {
    SafeFile out_file(out_file_path, "wb");

    unsigned int dictionary_size = dictionary.size();
    fwrite(&dictionary_size, sizeof(dictionary_size), 1, out_file.get());

    unsigned char vbe_buffer[8];
    for (std::string term : dictionary) {
        unsigned short term_size = term.size();                                 // 2 bytes
        unsigned int posting_list_size = posting_list_mapping[term].size();     // 4 bytes should be sufficient

        fwrite(&term_size, sizeof(term_size), 1, out_file.get());
        fwrite(&posting_list_size, sizeof(posting_list_size), 1, out_file.get());

        fwrite(term.c_str(), sizeof(char), term.size(), out_file.get());

        // VBE encoding
        unsigned long long last = 0;
        for (int i = 0; i < posting_list_mapping[term].size(); i++) {
            PostingItem item = posting_list_mapping[term][i];
            unsigned long long delta = item.doc_id - last;
            int encode_length = vbe_encode(delta, vbe_buffer);

            fwrite(vbe_buffer, sizeof(unsigned char), encode_length, out_file.get());
            fwrite(&item.freq, sizeof(item.freq), 1, out_file.get());
            last = item.doc_id;
        }
    }
}

void construct_inverted_blocks(
    const fs::path& in_dir,
    const fs::path& out_dir,
    const size_t mem_limit
) {
    std::unordered_map<std::string, PostingList> posting_list_mapping;
    std::vector<std::string> dictionary;

    std::vector<fs::path> token_streams = glob_files(in_dir, "", ".bin");
    sort(token_streams.begin(), token_streams.end());

    int partial_block_counter = 0;

    for (auto &token_stream : token_streams) {
        SafeFile fp(token_stream, "rb");

        while (build_partial_index(
            fp,
            mem_limit,
            posting_list_mapping,
            dictionary
        )) {
            write_partial_index(
                out_dir / std::format("block_{:04}.bin", partial_block_counter),
                posting_list_mapping,
                dictionary
            );

            ++partial_block_counter;
            posting_list_mapping.clear();
            dictionary.clear();
        }
    }
}
