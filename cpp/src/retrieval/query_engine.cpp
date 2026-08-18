/**
 * @file query_engine.cpp
 * 
 * @brief Query engine for Lexical retrieval
 * 
 */

#include "scholar_rank/retrieval/query_engine.h"
#include "scholar_rank/retrieval/bm25.h"
#include "scholar_rank/retrieval/file_names.h"
#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/retrieval/construct_doc_len_list.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/vbe.h"
#include "scholar_rank/utils/logger.h"

#include <vector>
#include <format>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <bit>
#include <queue>
#include <chrono>

namespace fs = std::filesystem;


PostingPointer::PostingPointer(
    const std::string& _term,
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
    deep_block_id = 0;
    cur_addr = term_meta->block_meta_list[0].start_addr;
    doc_id = term_meta->block_meta_list[0].doc_id;
    doc_pos = 0;
}

int PostingPointer::get_cur_block_size(const int block_id) const {
    if (block_id == static_cast<int>(term_meta->block_meta_list.size()) - 1) {
        return term_meta->doc_count - block_size * block_id;
    }
    return block_size;
}

unsigned int PostingPointer::get_doc_count() const {
    return term_meta->doc_count;
}

unsigned long long PostingPointer::get_doc_id() const {
    return doc_id;
}

unsigned long long PostingPointer::get_shallow_block_id() const {
    return cur_block_id;
}

unsigned long long PostingPointer::get_block_count() const {
    return term_meta->block_meta_list.size();
}

unsigned long long PostingPointer::get_next_block_doc_id() const {
    if (cur_block_id + 1 >= static_cast<int>(term_meta->block_meta_list.size())) {
        return MAX_DOC_ID;
    }
    else {
        return term_meta->block_meta_list[cur_block_id + 1].doc_id;
    }
}

float PostingPointer::get_term_upper_bound() const {
    return term_meta->term_ub;
}

float PostingPointer::get_block_upper_bound() const {
    if (cur_block_id == static_cast<int>(term_meta->block_meta_list.size())) {
        throw std::runtime_error(std::format(
            "Unable to get block upper_bound: cur_block_id {} exceeded range [0,{}).",
            cur_block_id, term_meta->block_meta_list.size()
        ));
    }
    return term_meta->block_meta_list[cur_block_id].block_ub;
}

unsigned int PostingPointer::get_frequency() const {
    if (doc_id == MAX_DOC_ID) {
        throw std::runtime_error(std::format(
            "Unable to get entry frequency for doc_id {}.",
            doc_id
        ));
    }
    unsigned long long delta; unsigned int freq;
    read_posting_entry(delta, freq);
    return freq;
}

void PostingPointer::next_shallow(const unsigned long long target_doc_id) {
    auto it = std::upper_bound(
        term_meta->block_meta_list.begin(),
        term_meta->block_meta_list.end(),
        target_doc_id,
        [&] (unsigned long long val, BlockMeta x) {
            return val < x.doc_id;
        }
    );
    int new_block = static_cast<int>((it - term_meta->block_meta_list.begin()) - 1);
    
    // Impossible given how BMW works.
    if (new_block < cur_block_id) {
        throw std::runtime_error(std::format(
            "Unable to advance PostingPointer: new_block id {} is less than cur_block id {}.",
            new_block, cur_block_id
        ));
    }

    cur_block_id = new_block;
}

void PostingPointer::next_shallow_deep(const unsigned long long target_doc_id) {
    auto it = std::upper_bound(
        term_meta->block_meta_list.begin(),
        term_meta->block_meta_list.end(),
        target_doc_id,
        [&] (unsigned long long val, BlockMeta x) {
            return val < x.doc_id;
        }
    );
    int new_block = static_cast<int>((it - term_meta->block_meta_list.begin()) - 1);
    
    // Impossible given how BMW works.
    if (new_block < cur_block_id) {
        throw std::runtime_error(std::format(
            "Unable to advance PostingPointer: new_block id {} is less than cur_block id {}.",
            new_block, cur_block_id
        ));
    }

    if (new_block > deep_block_id) {
        deep_block_id = new_block;
        cur_block_id = new_block;
        cur_addr = term_meta->block_meta_list[new_block].start_addr;
        doc_id = term_meta->block_meta_list[new_block].doc_id;
        doc_pos = 0;
    }
}

void PostingPointer::next(const unsigned long long target_doc_id) {
    next_shallow_deep(target_doc_id);

    if (doc_id >= target_doc_id) {
        return;
    }
    
    unsigned long long delta;
    unsigned int freq;
    cur_addr += read_posting_entry(delta, freq);
    for (int i = doc_pos+1; i < get_cur_block_size(deep_block_id); i++){
        size_t entry_size = read_posting_entry(delta, freq);
        doc_id += delta;
        ++doc_pos;

        if (doc_id >= target_doc_id) break;

        cur_addr += entry_size;
    }

    if (doc_id < target_doc_id) {
        ++deep_block_id;
        ++cur_block_id;
        if (deep_block_id == static_cast<int>(term_meta->block_meta_list.size())) {
            cur_addr = term_meta->end_addr;
            doc_id = MAX_DOC_ID;
            doc_pos = 0;
        }
        else {
            cur_addr = term_meta->block_meta_list[deep_block_id].start_addr;
            doc_id = term_meta->block_meta_list[deep_block_id].doc_id;
            doc_pos = 0;
        }
    }
}


// Return size of the entry (for posting iteration)
size_t PostingPointer::read_posting_entry(
    unsigned long long &delta,
    unsigned int &freq
) const {
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
    catch (const std::runtime_error&) {
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
        freq += static_cast<unsigned int>(buffer[i]) * (1<<(8*i));
    }
    return vbe_size + sizeof(unsigned int);
}

void sort_posting(std::vector<PostingPointer>& postings) {
    std::sort(postings.begin(), postings.end(), [&](const PostingPointer& a, const PostingPointer& b) {
        return a.get_doc_id() < b.get_doc_id();
    });
}

int find_pivot(std::vector<PostingPointer>& postings, const float theta) {
    float running_score = 0.0;
    for (int i = 0; i < static_cast<int>(postings.size()); i++) {
        running_score += postings[i].get_term_upper_bound();
        if (running_score > theta) {
            return i;
        }
    }
    return static_cast<int>(postings.size());

}

bool check_block_max(std::vector<PostingPointer>& postings, const int pivot, const float theta) {
    unsigned long long doc = postings[pivot].get_doc_id();
    
    float running_sum = 0.0;
    int idx = 0;
    while (idx < static_cast<int>(postings.size()) && postings[idx].get_doc_id() <= doc) {
        running_sum += postings[idx].get_block_upper_bound();
        ++idx;
    }
    return (running_sum > theta);
}

float evaluate_prefix(
    std::vector<PostingPointer>& postings,
    const int pivot,
    std::vector<unsigned int>& doc_len_list,
    const float avgdl,
    const float k1,
    const float b
) {
    float running_sum = 0.0;
    unsigned long long doc = postings[pivot].get_doc_id();
    int idx = 0;
    while (idx <static_cast<int>(postings.size()) && postings[idx].get_doc_id() <= doc) {
        // Prefix of postings has to be tied run of doc_id = doc
        if (postings[idx].get_doc_id() != doc) {
            throw std::runtime_error(std::format(
                "Unable to complete full evaluation for doc_id {}: postings[{}].get_doc_id() expected {}, found {}.",
                doc, idx, doc, postings[idx].get_doc_id()
            ));
        }

        const unsigned long long N = doc_len_list.size();
        const unsigned int df_t = postings[idx].get_doc_count();
        const unsigned int tf = postings[idx].get_frequency();
        const float doc_len = doc_len_list[postings[idx].get_doc_id()];

        running_sum += calc_BM25(N, df_t, tf, doc_len, avgdl, k1, b);
        ++idx;
    }
    return running_sum;
}

void advance_prefix(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long target_doc_id
) {
    unsigned long long doc = postings[pivot].get_doc_id();
    int idx = 0;
    while (idx < static_cast<int>(postings.size()) && postings[idx].get_doc_id() <= doc) {
        // Prefix of postings has to be tied run of doc_id = doc
        if (postings[idx].get_doc_id() != doc) {
            throw std::runtime_error(std::format(
                "Unable to complete full evaluation for doc_id {}: postings[{}].get_doc_id() expected {}, found {}.",
                doc, idx, doc, postings[idx].get_doc_id()
            ));
        }

        postings[idx].next(target_doc_id);

        ++idx;
    }
}

void advance_one_excluding(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long N,
    const unsigned long long target_doc_id
) {
    unsigned long long doc = postings[pivot].get_doc_id();
    
    unsigned int min_df = N+1;
    int advance_id = -1;

    int idx = 0;
    while (idx < static_cast<int>(postings.size()) && postings[idx].get_doc_id() < doc) {

        const unsigned int df = postings[idx].get_doc_count();
        if (df < min_df) {
            min_df = df;
            advance_id = idx;
        }

        ++idx;
    }
    if (advance_id == -1) {
        throw std::runtime_error(std::format(
            "Unable to find advancing index where doc_id < {}.",
            doc
        ));
    }
    postings[advance_id].next(target_doc_id);
}

unsigned long long get_new_candidate(
    std::vector<PostingPointer>& postings,
    const int pivot
) {
    unsigned long long doc = postings[pivot].get_doc_id();

    unsigned long long target_doc_id = MAX_DOC_ID;
    int idx = 0;
    while (idx < static_cast<int>(postings.size()) && postings[idx].get_doc_id() <= doc) {
        unsigned long long cand = postings[idx].get_next_block_doc_id();
        target_doc_id = std::min(target_doc_id, cand);
        ++idx;
    }

    if (idx < static_cast<int>(postings.size())) target_doc_id = std::min(target_doc_id, postings[idx].get_doc_id());

    return target_doc_id;
}

void advance_one_including(
    std::vector<PostingPointer>& postings,
    const int pivot,
    const unsigned long long N,
    const unsigned long long target_doc_id
) {
    unsigned long long doc = postings[pivot].get_doc_id();
    
    unsigned int min_df = N+1;
    int advance_id = -1;

    int idx = 0;
    while (idx < static_cast<int>(postings.size()) && postings[idx].get_doc_id() <= doc) {

        const unsigned int df = postings[idx].get_doc_count();
        if (df < min_df) {
            min_df = df;
            advance_id = idx;
        }

        ++idx;
    }
    if (advance_id == -1) {
        throw std::runtime_error(std::format(
            "Unable to find advancing index where doc_id < {}.",
            doc
        ));
    }
    postings[advance_id].next(target_doc_id);
}

class QueryEngine{
public:
    QueryEngine(
        const fs::path meta_path,
        const Logger& _logger
    ) : logger(_logger) {
        logger.log("Initializing QueryEngine...");
        logger.log("Loading metadata...");

        // Load metadata
        read_metadata(meta_path, in_path, doc_len_path, doc_len_meta_path, k1, b, block_size, split_size);

        logger.log("Finished loading metadata, loading doc_len_list...");

        // Load doc_len_list
        read_doc_len_list(doc_len_path, doc_len_list);
        const auto [total_docs, total_frequency] = read_doc_len_meta(doc_len_meta_path);

        // Compute avgdl
        avgdl = static_cast<float>(static_cast<double>(total_frequency)/static_cast<double>(total_docs));

        logger.log("Finished loading doc_len_list. Loading term_meta_mapping...");

        // Load term_meta_mapping
        std::vector<std::pair<std::string, TermMeta>> raw_term_meta_mapping;
        raw_term_meta_mapping = read_block_meta_file(
            in_path / file_names::BLOCK_META
        );

        for (const auto& [term,metadata] : raw_term_meta_mapping) {
            term_meta_mapping.emplace(std::move(term), std::move(metadata));
            
            unsigned int file_index = metadata.file_index;
            if (file_index_mapping.find(file_index) == file_index_mapping.end()) {
                file_index_mapping.emplace(
                    file_index,
                    in_path / file_names::posting_file_name(file_index)
                );
            }
        }
        logger.log("Finished loading term_meta_mapping. QueryEngine ready.");
    }

    std::vector<std::pair<float, unsigned long long>> query_exhaustive(
        const std::vector<std::string>& raw_terms,
        const int k
    ) {

        // Filtering terms not indexed
        std::vector<std::string> terms;
        for (const auto& term : raw_terms) {
            if (term_meta_mapping.find(term) != term_meta_mapping.end()) {
                terms.push_back(term);
            }
        }

        std::priority_queue<
            std::pair<float, unsigned long long>,
            std::vector<std::pair<float, unsigned long long>>,
            std::greater<std::pair<float, unsigned long long>>
        > top_k;

        for (int i = 0; i < k; i++) top_k.push(std::make_pair(-1, MAX_DOC_ID));

        std::vector<PostingPointer> postings;

        for (const auto& term : terms) {
            postings.push_back(PostingPointer(
                term, block_size, term_meta_mapping, file_index_mapping
            ));
        }

        for (unsigned long long epoch = 0; epoch < (1LL<<32); epoch++) {
            sort_posting(postings);

            if (postings.empty() || postings.front().get_doc_id() == MAX_DOC_ID) break;
            unsigned long long doc = postings.front().get_doc_id();

            float score = evaluate_prefix(
                postings,
                0,
                doc_len_list,
                avgdl,
                k1,
                b
            );

            if (score > top_k.top().first) {
                top_k.pop();
                top_k.push(std::make_pair(score, doc));
            }

            advance_prefix(postings, 0, doc + 1);
        }
        std::vector<std::pair<float, unsigned long long>> res;
        while (!top_k.empty()) {
            if (top_k.top().first != -1) res.push_back(top_k.top());
            top_k.pop();
        }
        std::reverse(res.begin(), res.end());
        return res;
    }

    std::vector<std::pair<float, unsigned long long>> query(
        const std::vector<std::string>& raw_terms,
        const int k
    ) {
        // Filtering terms not indexed
        std::vector<std::string> terms;
        for (const auto& term : raw_terms) {
            if (term_meta_mapping.find(term) != term_meta_mapping.end()) {
                terms.push_back(term);
            }
        }

        std::priority_queue<
            std::pair<float, unsigned long long>,
            std::vector<std::pair<float, unsigned long long>>,
            std::greater<std::pair<float, unsigned long long>>
        > top_k;

        for (int i = 0; i < k; i++) top_k.push(std::make_pair(-1, MAX_DOC_ID));

        std::vector<PostingPointer> postings;

        for (const auto& term : terms) {
            postings.push_back(PostingPointer(
                term, block_size, term_meta_mapping, file_index_mapping
            ));
        }

        for (unsigned long long epoch = 0; epoch < (1LL<<32); epoch++) {
            sort_posting(postings);
            
            float theta = top_k.top().first;
            int pivot = find_pivot(postings, theta);

            if (pivot == static_cast<int>(postings.size())) break;
            unsigned long long doc = postings[pivot].get_doc_id();
            if (doc == MAX_DOC_ID) break;
            
            for (int idx = 0; idx < pivot; idx++) {
                postings[idx].next_shallow(doc);
            }


            bool flag = check_block_max(postings, pivot, theta);
            if (flag) {
                if (postings[0].get_doc_id() == doc) {
                    float score = evaluate_prefix(
                        postings,
                        pivot,
                        doc_len_list,
                        avgdl,
                        k1,
                        b
                    );
                    if (score > theta) {
                        top_k.pop();
                        top_k.push(std::make_pair(score, doc));
                    }
                    advance_prefix(
                        postings,
                        pivot,
                        doc + 1
                    );
                }
                else {
                    advance_one_excluding(postings, pivot, doc_len_list.size(), doc);
                }
            }
            else {
                unsigned long long target_doc_id = get_new_candidate(postings, pivot);
                advance_one_including(postings, pivot, doc_len_list.size(), target_doc_id);
            }
        }

        std::vector<std::pair<float, unsigned long long>> res;
        while (!top_k.empty()) {
            if (top_k.top().first != -1) res.push_back(top_k.top());
            top_k.pop();
        }
        std::reverse(res.begin(), res.end());
        return res;
    }

private:
    Logger logger;
    fs::path in_path, doc_len_path, doc_len_meta_path;
    float k1, b;
    int block_size;
    size_t split_size;
    std::vector<unsigned int> doc_len_list;
    float avgdl;
    std::unordered_map<std::string, TermMeta> term_meta_mapping;
    std::unordered_map<unsigned int, SafeFileMmap> file_index_mapping;
};

std::pair<std::vector<std::pair<float, unsigned long long>>, std::chrono::duration<double, std::milli>> query (
    const fs::path meta_path,
    const std::vector<std::string> raw_terms,
    const int k
) {
    Logger logger(__FILE_NAME__, Logger::INFO);
    QueryEngine engine(meta_path, logger);

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::pair<float, unsigned long long>> result = engine.query(raw_terms, k);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    logger.log(std::format("Finished querying. Time elapsed: {}", elapsed));
    
    return std::make_pair(result,elapsed);
}

std::vector<std::vector<std::pair<float, unsigned long long>>> query_batch (
    const fs::path meta_path,
    const std::vector<std::vector<std::string>> raw_terms,
    const std::vector<int> k
) {
    Logger logger(__FILE_NAME__, Logger::INFO);
    QueryEngine engine(meta_path, logger);

    std::vector<std::vector<std::pair<float, unsigned long long>>> results;
    for (size_t i = 0; i < raw_terms.size(); i++) {
        auto start = std::chrono::high_resolution_clock::now();

        results.push_back(engine.query(raw_terms[i], k[i]));

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        logger.log(std::format("Finished query {}. Time elapsed: {}", i+1, elapsed));
    }

    return results;
}

std::pair<std::vector<std::vector<std::pair<float, unsigned long long>>>, 
        std::pair<std::chrono::duration<double, std::milli>, std::vector<std::chrono::duration<double, std::milli>>>>
        query_batch_benchmark (
    const fs::path meta_path,
    const std::vector<std::vector<std::string>> raw_terms,
    const std::vector<int> k
) {
    Logger logger(__FILE_NAME__, Logger::INFO);
    auto start_engine = std::chrono::high_resolution_clock::now();
    QueryEngine engine(meta_path, logger);
    auto end_engine = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_engine = end_engine - start_engine;

    std::vector<std::vector<std::pair<float, unsigned long long>>> results;
    std::vector<std::chrono::duration<double, std::milli>> benchmarks;
    for (size_t i = 0; i < raw_terms.size(); i++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        results.push_back(engine.query(raw_terms[i], k[i]));

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        logger.log(std::format("Finished query {}. Time elapsed: {}", i+1, elapsed));
        benchmarks.push_back(elapsed);
    }

    return std::make_pair(results, std::make_pair(elapsed_engine, benchmarks));
}

std::pair<std::vector<std::vector<std::pair<float, unsigned long long>>>, 
        std::pair<std::chrono::duration<double, std::milli>, std::vector<std::chrono::duration<double, std::milli>>>>
        query_batch_exhaustive_benchmark (
    const fs::path meta_path,
    const std::vector<std::vector<std::string>> raw_terms,
    const std::vector<int> k
) {
    Logger logger(__FILE_NAME__, Logger::INFO);
    auto start_engine = std::chrono::high_resolution_clock::now();
    QueryEngine engine(meta_path, logger);
    auto end_engine = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_engine = end_engine - start_engine;

    std::vector<std::vector<std::pair<float, unsigned long long>>> results;
    std::vector<std::chrono::duration<double, std::milli>> benchmarks;
    for (size_t i = 0; i < raw_terms.size(); i++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        results.push_back(engine.query_exhaustive(raw_terms[i], k[i]));

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        logger.log(std::format("Finished query {}. Time elapsed: {}", i+1, elapsed));
        benchmarks.push_back(elapsed);
    }

    return std::make_pair(results, std::make_pair(elapsed_engine, benchmarks));
}