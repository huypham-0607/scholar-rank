#include "scholar_rank/retrieval/bm25.h"

#include <cmath>

float bm25_saturation(
    const float k1,
    const float b,
    const float tf,
    const float doc_len,
    const float avgdl
) {
    float numerator = (k1 + 1.0f) * tf;
    float denominator = k1 * ((1.0f - b) + b * (doc_len / avgdl)) + tf;
    return numerator / denominator;
}

float calc_BM25(
    const unsigned long long N,
    const unsigned long long df_t,
    const float tf,
    const float doc_len,
    const float avgdl,
    const float k1,
    const float b
) {
    float idf = std::log((float)N / (float)df_t);
    return idf * bm25_saturation(k1, b, tf, doc_len, avgdl);
}
