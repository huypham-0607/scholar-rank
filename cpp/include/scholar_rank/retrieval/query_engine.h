#ifndef QUERY_ENGINE_H
#define QUERY_ENGINE_H

#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/file_io.h"

#include <limits>
#include <vector>
#include <unordered_map>

// A SafeMMAP class here for deep-pointer movement.

const unsigned long long MAX_DOC_ID = std::numeric_limits<unsigned long long>::max();

class PostingPointer {
public:
    PostingPointer(
        const std::string& _term,
        const int _block_size,
        std::unordered_map<std::string, TermMeta> &term_meta_mapping,
        std::unordered_map<unsigned int, SafeFileMmap> &file_index_mapping
    );

    int get_cur_block_size(const int block_id) const;
    unsigned int get_doc_count() const;
    unsigned long long get_doc_id() const;
    unsigned long long get_shallow_block_id() const;
    unsigned long long get_block_count() const;
    unsigned long long get_next_block_doc_id() const;
    float get_term_upper_bound() const;
    float get_block_upper_bound() const;
    unsigned int get_frequency() const;

    void next_shallow(const unsigned long long target_doc_id);
    void next_shallow_deep(const unsigned long long target_doc_id);
    void next(const unsigned long long target_doc_id);
    
private:
    std::string term;
    TermMeta* term_meta;
    SafeFileMmap* posting_file;
    int block_size;
    int cur_block_id;
    int deep_block_id;
    size_t cur_addr;
    unsigned long long doc_id;
    int doc_pos;

    size_t read_posting_entry(unsigned long long &delta, unsigned int &freq) const;
};

void sort_posting(std::vector<PostingPointer>& postings);

int find_pivot(std::vector<PostingPointer>& postings, const float theta);

bool check_block_max(std::vector<PostingPointer>& postings, const int pivot, const float theta);

float evaluate_prefix(
    std::vector<PostingPointer>& postings,
    const int pivot,
    std::vector<unsigned int>& doc_len_list,
    const float avgdl,
    const float k1,
    const float b
);

void advance_prefix(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long target_doc_id
);

void advance_one_excluding(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long N,
    const unsigned long long target_doc_id
);

unsigned long long get_new_candidate(
    std::vector<PostingPointer>& postings,
    const int pivot
);

void advance_one_including(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long N,
    const unsigned long long target_doc_id
);

std::pair<std::vector<std::pair<float, unsigned long long>>, std::chrono::duration<double, std::milli>> query (
    const fs::path meta_path,
    const std::vector<std::string> raw_terms,
    const int k
);

std::pair<std::vector<std::vector<std::pair<float, unsigned long long>>>, 
        std::vector<std::chrono::duration<double, std::milli>>> query_batch(
    const fs::path meta_path,
    const std::vector<std::vector<std::string>> raw_terms,
    const std::vector<int> k
);

#endif