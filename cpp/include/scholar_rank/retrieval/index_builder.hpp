#ifndef INDEX_BUILDER_HPP
#define INDEX_BUILDER_HPP

#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>

struct PostingItem {
    unsigned long long doc_id;
    unsigned int freq;

    PostingItem(long long _doc_id, int _freq);
};

class PostingList {
private:
    std::vector<PostingItem> list;
public:
    PostingList();

    bool has_document(long long doc_id);

    void add_document(long long doc_id);

    size_t size();

    const PostingItem& operator[] (size_t idx) const;
};

bool build_partial_index(
    FILE* token_stream,
    const size_t mem_limit,
    std::unordered_map<std::string, PostingList> &posting_list_mapping,
    std::vector<std::string> &dictionary
);

void write_partial_index(
    const std::filesystem::path out_file_path,
    std::unordered_map<std::string, PostingList>& posting_list_mapping,
    std::vector<std::string>& dictionary
);

void construct_inverted_blocks(
    const std::filesystem::path& in_dir,
    const std::filesystem::path& out_dir,
    const size_t mem_limit
);

#endif