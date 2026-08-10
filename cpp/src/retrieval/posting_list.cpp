#include "scholar_rank/retrieval/posting_list.h"

#include <stdexcept>

PostingItem::PostingItem(const unsigned long long _doc_id, const unsigned int _freq) : doc_id(_doc_id), freq(_freq) {}

const bool PostingItem::operator<(PostingItem other) const {
    if (doc_id == other.doc_id) {
        return freq < other.freq;
    }
    return doc_id < other.doc_id;
}

PostingList::PostingList() {
    list = std::vector<PostingItem>();
}

bool PostingList::has_document(const unsigned long long doc_id) const {
    return (!list.empty() && list.back().doc_id == doc_id);
}

void PostingList::add_document(const unsigned long long doc_id, const unsigned int freq) {
    // Assuming doc_id are monotonically increasing (ensured by token stream)
    // This is to ensure constant time complexity
    if (list.empty() || list.back().doc_id != doc_id) {
        list.push_back(PostingItem(doc_id, freq));
    }
    else {
        list.back().freq += freq;
    }
}

size_t PostingList::size() const {
    return list.size();
}

void PostingList::clear() {
    list.clear();
}

const PostingItem& PostingList::operator[] (size_t idx) const {
    if (idx >= list.size()) {
        throw std::out_of_range("Posting list index out of bounds.");
    }
    return list[idx];
}
