#include "scholar_rank/retrieval/token_stream.h"

#include <cstdio>
#include <stdexcept>

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
