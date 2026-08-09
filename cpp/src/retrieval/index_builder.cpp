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
    const SafeFile& token_stream,
    unsigned long long* const ptr_doc_id,
    std::string* const ptr_term
) {
    long initial_pos = ftell(token_stream.get());
    int arg_count = fread(ptr_doc_id, sizeof(*ptr_doc_id), 1, token_stream.get());
    if (arg_count != 1) {
        long bytes_read = ftell(token_stream.get()) - initial_pos;
        if (bytes_read == 0) return false;
        throw std::runtime_error("I/O error reading doc_id.");
    }

    unsigned short term_size;
    arg_count = fread(&term_size, sizeof(term_size), 1, token_stream.get());
    if (arg_count != 1) throw std::runtime_error("I/O error reading term_size.");
    
    if (term_size > MAX_TERM_LENGTH) throw std::runtime_error("Erroneous term_size.");
    ptr_term->resize(term_size);

    arg_count = fread(&(*ptr_term)[0], sizeof(char), term_size, token_stream.get());
    if (arg_count != term_size) throw std::runtime_error("I/O error reading term_value.");

    return true;
}

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
            // logger.log(std::format("delta:{}",delta));
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

void write_doc_len_entry(
    const SafeFile& out_fp,
    const unsigned long long& delta,
    const unsigned char& freq
) {
    // This depends on assertion that freq (from OpenAlex) never exceed (1<<8)
    unsigned char buffer[8];
    size_t encode_len = vbe_encode(delta, buffer);
    size_t arg_count;
    arg_count = fwrite(buffer, sizeof(unsigned char), encode_len, out_fp.get());
    if (arg_count != encode_len) throw std::runtime_error(std::format(
        "Error while writing doc_len entry."
    ));
    arg_count = fwrite(&freq, sizeof(freq), 1, out_fp.get());
    if (arg_count != 1) throw std::runtime_error(std::format(
        "Error while writing doc_len entry."
    ));
}


/**
 * @brief Construct doc_id - doc_len sequence
 * 
 * Format: (doc_id<vbe_encoding>)(doc_len<unsigned char>)
 * 
 * @param in_dir 
 * @param out_dir 
 */

void construct_doc_len_list(
    const fs::path& in_dir,
    const fs::path& out_dir
) {
    std::vector<fs::path> token_streams = glob_files(in_dir, "", ".bin");
    sort(token_streams.begin(), token_streams.end());

    fs::path out_file_path = out_dir / "doc_len_table.bin"; 

    SafeFile out_fp(out_file_path, "wb");

    bool has_started = false;

    unsigned long long delta = 0;
    unsigned long long prev_doc_id = 0;
    unsigned char running_freq = 0;
    for (auto &token_stream : token_streams) {
        SafeFile fp(token_stream, "rb");
        unsigned long long cur_doc_id = 0;
        std::string term;

        while (read_token(fp, &cur_doc_id, &term)) {
            if (!has_started || cur_doc_id != prev_doc_id) {
                if (has_started) {
                    write_doc_len_entry(out_fp, delta, running_freq);
                }
                has_started = true;
                delta = cur_doc_id - prev_doc_id;
                prev_doc_id = cur_doc_id;
                running_freq = 0;
            }
            ++running_freq;
        }
    }

    // Adding last element.
    if (has_started) {
        write_doc_len_entry(out_fp, delta, running_freq);
    }
}

bool read_doc_len_entry(
    const SafeFile& in_file,
    unsigned long long* const ptr_offset,
    unsigned char* const ptr_freq
) {
    size_t arg_count;
    long initial_pos = ftell(in_file.get());

    unsigned char buffer[8];
    bool res = read_vbe(in_file.get(), buffer);

    if (!res) {
        long bytes_read = ftell(in_file.get()) - initial_pos;
        if (bytes_read == 0) return false;
        throw std::runtime_error(std::format(
            "Error while reading doc_len entry."
        ));
    }
    (*ptr_offset) = vbe_decode(buffer);

    arg_count = fread(ptr_freq, sizeof(*ptr_freq), 1, in_file.get());
    if (arg_count != 1) throw std::runtime_error(std::format(
        "Error while reading doc_len entry."
    ));
    return true;
}

void read_doc_len_list(
    const fs::path& in_dir,
    std::vector<unsigned char>& doc_len_list
) {
    SafeFile in_file(in_dir,"rb");
    unsigned long long doc_id = 0;
    unsigned long long delta;
    unsigned char freq;
    while (read_doc_len_entry(in_file, &delta, &freq)) {
        doc_id += delta;
        doc_len_list.resize(std::max((size_t)doc_id + 1, doc_len_list.size()), 0);
        doc_len_list[doc_id] = freq;
    }
}

/**
 * @brief Merging partial posting blocks, into a final complete posting list.
 * 
 * Each posting (1 single term) will be treated as an atomic unit. We will
 * group these posting units into blocks of certain sizes.
 * 
 * Maintain a separate look-up list storing (file_id, position) tuple for fast
 * querying.
 * 
 * To remind, Standard BM25 implementation is:
 * 
 *  - log(N/df(Term)) * tf(term,doc)(k+1) / tf(term,doc) + k (1-b+b(|d|/avgdl))
 * 
 * For WAND scoring function \alpha_{t} * w(t,d)
 * - \alpha_{t} is our IDF (log(N/df(Term)))
 * - w(t,d) is tf(term,doc)(k+1) / tf(term,doc) + k (1-b+b(|d|/avgdl)).
 * 
 * For each posting list, we have to compute
 * -
 * 
 * @param in_dir 
 * @param out_dir 
 */


// <term_size><posting_list_size><term><<vbe_encoding_{i}><freq_{i}>>

class Stream {
public:
    Stream(const fs::path in_path) : in_file(SafeFile(in_path, "rb")) {
        is_empty = false;

        in_file.fread(&dict_size, sizeof(dict_size), 1);
        if (dict_size == 0) {
            is_empty = true;
        }
        else {
            list_id = 0;

            // Setting up first posting_list
            unsigned short term_size;
            in_file.fread(&term_size, sizeof(term_size), 1);
            in_file.fread(&list_size, sizeof(list_size), 1);
            term.clear(); term.resize(term_size);
            in_file.fread(&(term[0]), sizeof(char), term_size);

            // Setting up first posting_item (posting list should be non-empty)
            item_id = 0;
            doc_id = 0;
            unsigned char buffer[BUFFER_LIMIT];
            read_vbe(in_file.get(), buffer);
            doc_id += vbe_decode(buffer);
            in_file.fread(&freq, sizeof(freq), 1);
        }
    }

    bool empty() const {
        return is_empty;
    }

    PostingItem get_item() const {
        if (is_empty) {
            throw std::runtime_error(std::format(
                "Accessing item in an empty Stream"
            ));
        }
        return PostingItem(doc_id, freq);
    }

    unsigned int get_item_id() const {
        if (is_empty) {
            throw std::runtime_error(std::format(
                "Accessing item_id in an empty Stream"
            ));
        }
        return item_id;
    }

    unsigned int get_list_size() const {
        if (is_empty) {
            throw std::runtime_error(std::format(
                "Accessing list_size in an empty Stream"
            ));
        }
        return list_size;
    }

    std::string get_term() const {
        if (is_empty) {
            throw std::runtime_error(std::format(
                "Accessing term in an empty Stream"
            ));
        }
        return term;
    }

    unsigned int get_list_id() const {
        if (is_empty) {
            throw std::runtime_error(std::format(
                "Accessing list_id in an empty Stream"
            ));
        }
        return list_id;
    }

    unsigned int get_dict_size() const {
        return dict_size;
    }

    /*
     * Returns true if next item is available (not empty).
     */
    bool next() {
        if (is_empty) return false;
        ++item_id;
        if (item_id == list_size) {
            ++list_id;
            if (list_id == dict_size) {
                is_empty = true;
                return false;
            }

            unsigned short term_size;
            in_file.fread(&term_size, sizeof(term_size), 1);
            in_file.fread(&list_size, sizeof(list_size), 1);
            term.clear(); term.resize(term_size);
            in_file.fread(&(term[0]), sizeof(char), term_size);

            item_id = 0;
            doc_id = 0;
        }

        unsigned char buffer[BUFFER_LIMIT];
        read_vbe(in_file.get(), buffer);
        doc_id += vbe_decode(buffer);
        in_file.fread(&freq, sizeof(freq), 1);
        return true;
    }

private:
    SafeFile in_file;
    bool is_empty;
    unsigned int dict_size;

    unsigned int list_id;
    unsigned int list_size;
    std::string term;

    unsigned int item_id;
    unsigned long long doc_id;
    unsigned int freq;
};

void merge_inverted_blocks(
    const fs::path& doc_len_dir,
    const fs::path& in_dir,
    const fs::path& out_dir,
    const int block_size = 128
) {
    std::vector<unsigned char> doc_len_list;

    read_doc_len_list(doc_len_dir, doc_len_list);

    unsigned long long total_doc_length = 0;

    // Assuming doc_ids are mapped to [0,N)
    for (unsigned long long i = 0; i < doc_len_list.size(); i++) {
        total_doc_length += doc_len_list[i];
    }

    long double avgdl = (long double)total_doc_length/doc_len_list.size();

    std::vector<fs::path> in_paths = glob_files(in_dir, "", ".bin");
    sort(in_paths.begin(), in_paths.end());

    std::vector<SafeFile> streams;
    for (fs::path in_path : in_paths) {
        streams.push_back(SafeFile(in_path, "rb"));
    }    
}