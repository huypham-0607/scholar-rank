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

#include "scholar_rank/utils/vbe.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <stdexcept>

constexpr unsigned int MAX_TERM_LENGTH = 256;       // Might change depending on dataset
constexpr unsigned int EST_UMAP_MEM_PER_ENTRY = 48; // Safe estimation of std::unordered_map mem usage per entry

struct PostingItem {
    unsigned long long doc_id;
    unsigned int freq;

    PostingItem(long long _doc_id, int _freq) : doc_id(_doc_id), freq(_freq) {}
};

class PostingList {
private:
    std::vector<PostingItem> list;
public:
    PostingList() {
        list = std::vector<PostingItem>();
    }

    bool has_document(long long doc_id) {
        return (!list.empty() && list.back().doc_id == doc_id);
    }

    void add_document(long long doc_id) {
        // Assuming doc_id are monotonically increasing (ensured by token stream)
        if (list.empty() || list.back().doc_id != doc_id) {
            list.push_back(PostingItem(doc_id, 1));
        }
        else {
            list.back().freq++;
        }
    }

    size_t size() {
        return list.size();
    }

    const PostingItem& operator[] (size_t idx) const {
        if (idx >= list.size()) {
            throw std::out_of_range("Posting list index out of bounds.");
        }
        return list[idx];
    }
};

bool readToken(FILE *token_stream, unsigned long long *ptr_doc_id, std::string *ptr_term) {
    int arg_count = fread(ptr_doc_id, sizeof(ptr_doc_id), 1, token_stream);
    if (!arg_count) {
        if (feof(token_stream)) return false;
        throw std::runtime_error("I/O error reading doc_id.");
    }

    unsigned short term_size;
    arg_count = fread(&term_size, sizeof(term_size), 1, token_stream);;
    if (!arg_count) throw std::runtime_error("I/O error reading term_size.");
    
    if (term_size > MAX_TERM_LENGTH) throw std::runtime_error("Erroneous term_size.");
    ptr_term->resize(term_size);

    arg_count = fread(&(*ptr_term)[0], sizeof(char), term_size, token_stream);
    if (arg_count != term_size) throw std::runtime_error("I/O error reading term.");

    return true;
}

void spimi_invert(FILE *token_stream, std::string out_file_name, size_t mem_limit) {
    std::unordered_map<std::string, PostingList> posting_list_mapping;
    std::vector<std::string> dictionary;

    size_t mem_usage = 0;

    unsigned long long cur_doc_id; 
    std::string cur_term;
    
    while ((mem_usage < mem_limit/5*4) && readToken(token_stream, &cur_doc_id, &cur_term)) {
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
    FILE *out_file = std::fopen(out_file_name.c_str(), "wb");

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
            int encode_length = vbe_encode(delta, vbe_buffer);

            fwrite(vbe_buffer, sizeof(unsigned char), encode_length, out_file);
            fwrite(&item.freq, sizeof(item.freq), 1, out_file);
        }
    }

    fclose(out_file);
}

/* 
 * For each posting:
 * 
 * 
 */ 

void spimi_merge(FILE *token_stream) {}