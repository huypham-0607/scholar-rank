/**
 * @file index_builder.cpp
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2026-08-01
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include "scholar_rank/retrieval/index_builder.h"
#include "scholar_rank/utils/vbe.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/logger.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

// Compiler specific
Logger logger(__FILE_NAME__, Logger::DEBUG);

PostingItem::PostingItem(const long long _doc_id, const unsigned int _freq) : doc_id(_doc_id), freq(_freq) {}

PostingList::PostingList() {
    list = std::vector<PostingItem>();
}

bool PostingList::has_document(const long long doc_id) const {
    return (!list.empty() && list.back().doc_id == doc_id);
}

void PostingList::add_document(const long long doc_id) {
    // Assuming doc_id are monotonically increasing (ensured by token stream)
    // This is to ensure constant time complexity
    if (list.empty() || list.back().doc_id != doc_id) {
        list.push_back(PostingItem(doc_id, 1));
    }
    else {
        list.back().freq++;
    }
}

size_t PostingList::size() const {
    return list.size();
}

const PostingItem& PostingList::operator[] (size_t idx) const {
    if (idx >= list.size()) {
        throw std::out_of_range("Posting list index out of bounds.");
    }
    return list[idx];
}


/**
 * @brief Read a single doc_id - term pair
 * 
 * token_stream is a binary stream, with layout:
 * 
 *      <doc_id><term_length><term_value>...
 * 
 * Each character in term_value is 1 byte (ASCII)
 * 
 * @param token_stream 
 * @param ptr_doc_id 
 * @param ptr_term 
 * @return true 
 * @return false 
 */
bool read_token(
    FILE* const token_stream,
    unsigned long long* const ptr_doc_id,
    std::string* const ptr_term
) {
    if (token_stream == NULL) {
        throw std::runtime_error("Invalid token stream.");
    }

    long initial_pos = ftell(token_stream);
    int arg_count = fread(ptr_doc_id, sizeof(*ptr_doc_id), 1, token_stream);
    if (arg_count != 1) {
        long bytes_read = ftell(token_stream) - initial_pos;
        if (bytes_read == 0) return false;
        throw std::runtime_error("I/O error reading doc_id.");
    }

    unsigned short term_size;
    arg_count = fread(&term_size, sizeof(term_size), 1, token_stream);;
    if (arg_count != 1) throw std::runtime_error("I/O error reading term_size.");
    
    if (term_size > MAX_TERM_LENGTH) throw std::runtime_error("Erroneous term_size.");
    ptr_term->resize(term_size);

    arg_count = fread(&(*ptr_term)[0], sizeof(char), term_size, token_stream);
    if (arg_count != term_size) throw std::runtime_error("I/O error reading term_value.");

    return true;
}

bool build_partial_index(
    FILE* const token_stream,
    const size_t mem_limit,
    std::unordered_map<std::string, PostingList> &posting_list_mapping,
    std::vector<std::string> &dictionary
) {
    if (token_stream == NULL) {
        throw std::runtime_error("Invalid token stream.");
    }
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
 * Starts with a dictionary_size
 * Format for each posting list:
 * 
 *       <term_size><posting_list_size><term><<vbe_encoding_{i}><freq_{i}>>
 * 
 * - term_size:             unsigned short
 * - posting_list_size:     unsigned int
 * - term:                  char[]
 * - vbe_encoding_{i}:      unsigned char[]
 * - freq:                  unsigned int
 * 
 * @param out_file_path
 * @param posting_list_mapping
 * @param dictionary
 */
void write_partial_index(
    const fs::path out_file_path,
    std::unordered_map<std::string, PostingList>& posting_list_mapping,
    std::vector<std::string>& dictionary
) {
    FILE *out_file = std::fopen(out_file_path.string().c_str(), "wb");

    if (out_file == NULL) throw std::runtime_error(
        std::format("Failed to open file {}.", out_file_path.string())
    );

    unsigned int dictionary_size = dictionary.size();
    fwrite(&dictionary_size, sizeof(dictionary_size), 1, out_file);

    unsigned char vbe_buffer[8];
    for (std::string term : dictionary) {
        unsigned short term_size = term.size();                                 // 2 bytes
        unsigned int posting_list_size = posting_list_mapping[term].size();     // 4 bytes should be sufficient
        
        fwrite(&term_size, sizeof(term_size), 1, out_file);
        fwrite(&posting_list_size, sizeof(posting_list_size), 1, out_file);
        
        fwrite(term.c_str(), sizeof(char), term.size(), out_file);

        // VBE encoding
        unsigned long long last = 0;
        for (int i = 0; i < posting_list_mapping[term].size(); i++) {
            PostingItem item = posting_list_mapping[term][i];
            unsigned long long delta = item.doc_id - last;
            // logger.log(std::format("delta:{}",delta));
            int encode_length = vbe_encode(delta, vbe_buffer);

            fwrite(vbe_buffer, sizeof(unsigned char), encode_length, out_file);
            fwrite(&item.freq, sizeof(item.freq), 1, out_file);
        }
    }

    fclose(out_file);
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
        FILE *fp = std::fopen(token_stream.string().c_str(), "rb");

        if (fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", token_stream.string())
        );

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
        fclose(fp);
    }
}