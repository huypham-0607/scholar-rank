#ifndef INDEX_BUILDER_H
#define INDEX_BUILDER_H

#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>

constexpr unsigned int MAX_TERM_LENGTH = 256;       // Might change depending on dataset
constexpr unsigned int EST_UMAP_MEM_PER_ENTRY = 48; // Safe estimation of std::unordered_map mem usage per entry
constexpr unsigned int TOKEN_STREAM_LIMIT = 10000;  // Max number of token streams accepted

constexpr std::string BLOCK_PREFIX = "token_";

struct PostingItem {
    unsigned long long doc_id;
    unsigned int freq;

    PostingItem(const long long _doc_id, const unsigned int _freq);
};

class PostingList {
private:
    std::vector<PostingItem> list;
public:
    PostingList();

    bool has_document(const long long doc_id) const;

    void add_document(const long long doc_id);

    size_t size() const;

    const PostingItem& operator[] (size_t idx) const;
};

bool read_token(
    FILE* const token_stream,
    unsigned long long* const ptr_doc_id,
    std::string* const ptr_term
);

bool build_partial_index(
    FILE* const token_stream,
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