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

#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

constexpr int MAX_TERM_LENGTH = 256;

struct PostingItem {
    long long doc_id;
    int freq;

    PostingItem(long long _doc_id, int _freq) : doc_id()(_doc_id), freq(_freq) {}
};

class PostingList {
private:
    std::vector<PostingItem> list;
public:
    PostingList() {
        list = std::vector<PostingItem>();
    }

    void add_document(long long doc_id) {
        // Assuming docId are monotonically increasing (ensured by token stream)
        if (list.empty() || list.back().doc_id != doc_id) {
            list.push_back(PostingItem(doc_id, 1));
        }
        else {
            list.back().freq++;
        }
    }
};

bool readToken(FILE *token_stream, long long *ptr_doc_id, std::string *ptr_term) {
    int arg_count = fread(ptr_doc_id, sizeof(ptr_doc_id), 1, token_stream);
    if (!arg_count) {
        if (feof(token_stream)) return false;
        throw std::runtime_error("I/O error reading doc_id.");
    }

    short term_size;
    arg_count = fread(&term_size, sizeof(term_size), 1, token_stream);;
    if (!arg_count) throw std::runtime_error("I/O error reading term_size.");
    
    if (term_size > MAX_TERM_LENGTH) throw std::runtime_error("Erroneous term_size.");
    ptr_term->resize(term_size);

    arg_count = fread(&(*ptr_term)[0], sizeof(char), term_size, token_stream);
    if (arg_count != term_size) throw std::runtime_error("I/O error reading term.");

    return true;
}

void spimi_invert(FILE *token_stream, std::string out_file, size_t mem_limit) {
    std::unordered_map<std::string, PostingList> postingListMapping;
    std::vector<std::string> dictionary;

    size_t memUsage = 0;

    long long curDocId; 
    std::string curTerm;
    
    while (readToken(token_stream, &curDocId, &curTerm)) {
            
    }
}