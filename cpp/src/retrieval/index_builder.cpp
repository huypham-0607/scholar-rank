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

struct PostingItem {
    long long docId;
    int freq;

    PostingItem(long long _docId, int _freq) : docId(_docId), freq(_freq) {}
};

class PostingList {
private:
    std::vector<PostingItem> list;
public:
    PostingList() {
        list = std::vector<PostingItem>();
    }

    void addDocument(long long docId) {
        // Assuming docId are monotonically increasing (ensured by token stream)
        if (list.empty() || list.back().docId != docId) {
            list.push_back(PostingItem(docId, 1));
        }
        else {
            list.back().freq++;
        }
    }
};

void spimi_invert(FILE *token_stream, std::string out_file) {
    std::unordered_map<std::string, PostingList> postingListMapping;
    std::vector<std::string> dictionary;

    
}