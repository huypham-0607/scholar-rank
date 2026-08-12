/**
 * @file query_engine.cpp
 * 
 * @brief Query engine for Lexical retrieval
 * 
 */

#include "scholar_rank/retrieval/query_engine.h"
#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/vbe.h"

#include <vector>
#include <format>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <bit>

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
    const fs::path meta_path,
    const std::vector<std::string> query
) {
    fs::path in_path, doc_len_path;
    float k1, b;
    int block_size;
    size_t split_size;

    // Load metadata
    read_metadata(meta_path, in_path, doc_len_path, k1, b, block_size, split_size);

    // Load term_meta_mapping
    std::vector<std::pair<std::string, TermMeta>> raw_term_meta_mapping = read_block_meta_file(
        in_path
    );
    std::unordered_map<std::string, TermMeta> term_meta_mapping;
    for (auto [term,metadata] : raw_term_meta_mapping) {
        term_meta_mapping[term] = metadata;
    }

    // Filtering terms not indexed
    std::vector<std::string> terms;
    for (const auto& term : query) {
        if (term_meta_mapping.find(term) != term_meta_mapping.end()) {
            terms.push_back(term);
        }
    }

    std::unordered_map<unsigned int, SafeFileMmap> file_index_mapping;
   
}

class PostingPointer {
public:
    PostingPointer(
        const std::string _term,
        const int _block_size,
        std::unordered_map<std::string, TermMeta> &term_meta_mapping,
        std::unordered_map<unsigned int, SafeFileMmap> &file_index_mapping
    ) : term(_term) {
        if (term_meta_mapping.find(term) == term_meta_mapping.end()) {
            throw std::runtime_error(std::format(
                "Unable to initialize PostingPointer: Term {} not found in term_meta_mapping",
                term
            ));
        }
        term_meta = &(term_meta_mapping[term]);
        auto file_it = file_index_mapping.find(term_meta->file_index);
        if (file_it == file_index_mapping.end()) {
            throw std::runtime_error(std::format(
                "Unable to initialize PostingPointer: file_index {} not found in file_index_mapping",
                term_meta->file_index
            ));    
        }
        block_size = _block_size;
        posting_file = &(file_it->second);
        cur_block_id = 0;
        cur_addr = term_meta->block_meta_list[0].start_addr;
        doc_id = term_meta->block_meta_list[0].doc_id;
        doc_pos = 0;
    }

    int get_block_size(const int block_id) {
        if (block_id == term_meta->block_meta_list.size() - 1) {
            return term_meta->doc_count - block_size * block_id;
        }
        return block_size;
    }

    void next_shallow(const unsigned long long target_doc_id) {
        auto it = std::upper_bound(
            term_meta->block_meta_list.begin(),
            term_meta->block_meta_list.end(),
            target_doc_id,
            [&] (unsigned long long val, BlockMeta x) {
                return val < x.doc_id;
            }
        );
        int new_block = (it - term_meta->block_meta_list.begin()) - 1;
        
        // Impossible given how BMW works.
        if (new_block < cur_block_id) {
            throw std::runtime_error(std::format(
                "Unable to advance PostingPointer: new_block id {} is less than cur_block id {}.",
                new_block, cur_block_id
            ));
        }

        if (new_block > cur_block_id) {
            cur_block_id = new_block;
            cur_addr = term_meta->block_meta_list[new_block].start_addr;
            doc_id = term_meta->block_meta_list[new_block].doc_id;
            doc_pos = 0;
        }
    }

    void next(const unsigned long long target_doc_id) {
        next_shallow(target_doc_id);

        if (doc_id >= target_doc_id) {
            return;
        }
        
        unsigned long long delta;
        unsigned int freq;
        cur_addr += read_posting_entry(delta, freq);
        for (int i = doc_pos+1; i < get_block_size(cur_block_id); i++){
            size_t entry_size = read_posting_entry(delta, freq);
            doc_id += delta;
            ++doc_pos;

            if (doc_id >= target_doc_id) break;

            cur_addr += entry_size;
        }

        if (doc_id < target_doc_id) {
            ++cur_block_id;
            if (cur_block_id == term_meta->block_meta_list.size()) {
                cur_addr = term_meta->end_addr;
                doc_id = MAX_DOC_ID;
                doc_pos = 0;
            }
            else {
                cur_addr = term_meta->block_meta_list[cur_block_id].start_addr;
                doc_id = term_meta->block_meta_list[cur_block_id].doc_id;
                doc_pos = 0;
            }
        }
    }
    
private:
    // Flagging this because behaviour when the mapped item is erased from unordered_map
    std::string term;
    TermMeta* term_meta;
    SafeFileMmap* posting_file;
    int block_size;
    int cur_block_id;
    size_t cur_addr;
    unsigned long long doc_id;
    int doc_pos;

    // Return size of the entry (for posting iteration)
    size_t read_posting_entry(
        unsigned long long &delta,
        unsigned int &freq
    ) {
        // Read VBE encoding
        int idx = 0;
        unsigned char buffer[BUFFER_LIMIT];
        while (idx < BUFFER_LIMIT) {
            buffer[idx] = (*posting_file)[cur_addr + idx];
            if (buffer[idx] >= 128) {
                break;
            }
            ++idx;
        }

        // VBE overflow
        if (idx == BUFFER_LIMIT) {
            throw std::runtime_error(std::format(
                "Unable to read VBE Encoding in posting {}.",
                term
            ));
        }
        size_t vbe_size = idx + 1;

        try {
            delta = vbe_decode(buffer);
        }
        catch (std::runtime_error) {
            throw std::runtime_error(std::format(
                "Unable to convert VBE Encoding into 'unsigned long long' in posting {}.",
                term
            ));
        }

        // Reading frequency
        for (size_t i = 0; i < sizeof(unsigned int); i++) {
            buffer[i] = (*posting_file)[cur_addr + vbe_size + i];
        }
        if constexpr (std::endian::native == std::endian::big) {
            std::reverse(buffer, buffer + sizeof(unsigned int));
        }
        freq = 0;
        for (size_t i = 0; i < sizeof(unsigned int); i++) {
            freq += buffer[i] * (1<<(8*i));
        }
        return vbe_size + sizeof(unsigned int);
    } 
};