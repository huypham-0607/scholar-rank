#include "scholar_rank/retrieval/bm25.h"

#include <cmath>
#include <gtest/gtest.h>

TEST(BM25SaturationTest, AtAverageDocLengthSimplifiesToClassicSaturation) {
    // doc_len == avgdl makes the length-normalization term (1-b)+b*(dl/avgdl)
    // collapse to exactly 1, so saturation should reduce to (k1+1)*tf/(k1+tf).
    float k1 = 1.2f, b = 0.75f, tf = 3.0f, avgdl = 10.0f;
    float expected = (k1 + 1.0f) * tf / (k1 + tf);
    ASSERT_NEAR(bm25_saturation(k1, b, tf, /*doc_len=*/avgdl, avgdl), expected, 1e-5);
}

TEST(BM25SaturationTest, BZeroMakesDocLengthIrrelevant) {
    // b=0 disables length normalization entirely - doc_len shouldn't affect
    // the result at all.
    float k1 = 1.2f, b = 0.0f, tf = 2.0f, avgdl = 1.0f;
    float short_doc = bm25_saturation(k1, b, tf, /*doc_len=*/1.0f, avgdl);
    float long_doc = bm25_saturation(k1, b, tf, /*doc_len=*/999999.0f, avgdl);
    ASSERT_NEAR(short_doc, long_doc, 1e-5);
}

TEST(BM25SaturationTest, ExactHandComputedValue) {
    // k1=2, b=1, tf=1, doc_len=4, avgdl=2:
    // denom = 2*((1-1) + 1*(4/2)) + 1 = 2*2 + 1 = 5
    // num   = (2+1)*1 = 3
    // saturation = 3/5 = 0.6
    ASSERT_NEAR(bm25_saturation(2.0f, 1.0f, 1.0f, 4.0f, 2.0f), 0.6f, 1e-6);
}

TEST(BM25SaturationTest, MonotonicIncreasingInTfWithDiminishingReturns) {
    float k1 = 1.2f, b = 0.75f, doc_len = 5.0f, avgdl = 5.0f;
    float s1 = bm25_saturation(k1, b, 1.0f, doc_len, avgdl);
    float s2 = bm25_saturation(k1, b, 2.0f, doc_len, avgdl);
    float s3 = bm25_saturation(k1, b, 3.0f, doc_len, avgdl);
    float s4 = bm25_saturation(k1, b, 4.0f, doc_len, avgdl);

    ASSERT_LT(s1, s2);
    ASSERT_LT(s2, s3);
    ASSERT_LT(s3, s4);

    // Saturating curve: each successive increment should be smaller than the last.
    ASSERT_LT(s4 - s3, s3 - s2);
    ASSERT_LT(s3 - s2, s2 - s1);
}

TEST(BM25SaturationTest, MonotonicDecreasingInDocLength) {
    // Longer-than-average documents should be penalized (lower saturation
    // for the same tf), when b > 0.
    float k1 = 1.2f, b = 0.75f, tf = 2.0f, avgdl = 10.0f;
    float short_doc = bm25_saturation(k1, b, tf, /*doc_len=*/5.0f, avgdl);
    float avg_doc = bm25_saturation(k1, b, tf, /*doc_len=*/10.0f, avgdl);
    float long_doc = bm25_saturation(k1, b, tf, /*doc_len=*/20.0f, avgdl);

    ASSERT_GT(short_doc, avg_doc);
    ASSERT_GT(avg_doc, long_doc);
}

TEST(CalcBM25Test, ZeroIdfWhenTermInEveryDocument) {
    // df_t == N means the term carries no discriminative information -
    // log(N/df_t) = log(1) = 0, so the score must be exactly zero
    // regardless of tf/doc_len.
    float score = calc_BM25(/*N=*/100, /*df_t=*/100, /*tf=*/50.0f, /*doc_len=*/3.0f, /*avgdl=*/10.0f);
    ASSERT_NEAR(score, 0.0f, 1e-6);
}

TEST(CalcBM25Test, EqualsIdfTimesSaturation) {
    unsigned long long N = 100, df_t = 50;
    float tf = 3.0f, doc_len = 10.0f, avgdl = 10.0f, k1 = 1.2f, b = 0.75f;

    float expected_idf = std::log((float)N / (float)df_t);
    float expected = expected_idf * bm25_saturation(k1, b, tf, doc_len, avgdl);

    ASSERT_NEAR(calc_BM25(N, df_t, tf, doc_len, avgdl, k1, b), expected, 1e-5);
}

TEST(CalcBM25Test, RarerTermsScoreHigherAllElseEqual) {
    float tf = 2.0f, doc_len = 8.0f, avgdl = 8.0f;
    float common = calc_BM25(/*N=*/1000, /*df_t=*/500, tf, doc_len, avgdl);
    float rare = calc_BM25(/*N=*/1000, /*df_t=*/5, tf, doc_len, avgdl);

    ASSERT_GT(rare, common);
}
