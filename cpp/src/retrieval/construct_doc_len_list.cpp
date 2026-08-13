#include "scholar_rank/retrieval/construct_doc_len_list.h"
#include "scholar_rank/retrieval/file_names.h"
#include "scholar_rank/retrieval/token_stream.h"
#include "scholar_rank/utils/vbe.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <stdexcept>

namespace fs = std::filesystem;

void write_doc_len_entry(
    const SafeFile& out_fp,
    const unsigned long long& delta,
    const unsigned int& freq
) {
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

void construct_doc_len_list(
    const fs::path& in_dir,
    const fs::path& out_dir
) {
    std::vector<fs::path> token_streams = glob_files(in_dir, "", file_names::PARTIAL_BLOCK_EXT);
    sort(token_streams.begin(), token_streams.end());

    fs::path out_file_path = out_dir / file_names::DOC_LEN_LIST;

    SafeFile out_fp(out_file_path, "wb");

    bool has_started = false;

    unsigned long long delta = 0;
    unsigned long long prev_doc_id = 0;
    unsigned int running_freq = 0;
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
    unsigned int* const ptr_freq
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
    std::vector<unsigned int>& doc_len_list
) {
    SafeFile in_file(in_dir, "rb");
    unsigned long long doc_id = 0;
    unsigned long long delta;
    unsigned int freq;
    while (read_doc_len_entry(in_file, &delta, &freq)) {
        doc_id += delta;
        doc_len_list.resize(std::max((size_t)doc_id + 1, doc_len_list.size()), 0);
        doc_len_list[doc_id] = freq;
    }
}
