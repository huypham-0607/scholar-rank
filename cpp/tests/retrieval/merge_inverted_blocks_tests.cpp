#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/vbe.h"

#include <cstdio>
#include <filesystem>
#include <format>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

fs::path makeUniqueTempDir() {
    fs::path base = fs::temp_directory_path();
    for (int i = 0; i < 100; ++i) {
        auto candidate = base / (
            "gtest_" + std::to_string(::getpid())
            + "_" + std::to_string(i)
            + "_" + std::to_string(std::rand())
        );
        if (fs::exists(candidate)) continue;
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) return candidate;
    }
    throw std::runtime_error("could not create temp dir");
}

namespace MergeInvertedBlocksTest {
    class MergeInvertedBlocksTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
            block_dir = tmp_path / "blocks";
            doclen_dir = tmp_path / "doclen";
            merge_dir = tmp_path / "merged";
            fs::create_directory(block_dir);
            fs::create_directory(doclen_dir);
            fs::create_directory(merge_dir);
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path, block_dir, doclen_dir, merge_dir;

        // Writes a partial block (write_partial_index / Stream wire format)
        // containing a single term's postings.
        void write_raw_block(
            const std::string& file_name,
            const std::string& term,
            const std::vector<std::pair<unsigned long long, unsigned int>>& postings
        ) {
            SafeFile out(block_dir / file_name, "wb");
            unsigned int dict_size = 1;
            fwrite(&dict_size, sizeof(dict_size), 1, out.get());

            unsigned short term_size = term.size();
            unsigned int posting_list_size = postings.size();
            fwrite(&term_size, sizeof(term_size), 1, out.get());
            fwrite(&posting_list_size, sizeof(posting_list_size), 1, out.get());
            fwrite(term.c_str(), sizeof(char), term.size(), out.get());

            unsigned long long last = 0;
            unsigned char buf[BUFFER_LIMIT];
            for (auto [doc_id, freq] : postings) {
                int len = vbe_encode(doc_id - last, buf);
                fwrite(buf, sizeof(unsigned char), len, out.get());
                fwrite(&freq, sizeof(freq), 1, out.get());
                last = doc_id;
            }
        }

        // Writes a partial block containing multiple terms, dictionary
        // order matching the order given (mirrors write_partial_index,
        // which requires the dictionary to be sorted for a real one).
        void write_raw_block_multi(
            const std::string& file_name,
            const std::vector<std::pair<std::string, std::vector<std::pair<unsigned long long, unsigned int>>>>& terms
        ) {
            SafeFile out(block_dir / file_name, "wb");
            unsigned int dict_size = terms.size();
            fwrite(&dict_size, sizeof(dict_size), 1, out.get());

            for (const auto& [term, postings] : terms) {
                unsigned short term_size = term.size();
                unsigned int posting_list_size = postings.size();
                fwrite(&term_size, sizeof(term_size), 1, out.get());
                fwrite(&posting_list_size, sizeof(posting_list_size), 1, out.get());
                fwrite(term.c_str(), sizeof(char), term.size(), out.get());

                unsigned long long last = 0;
                unsigned char buf[BUFFER_LIMIT];
                for (auto [doc_id, freq] : postings) {
                    int len = vbe_encode(doc_id - last, buf);
                    fwrite(buf, sizeof(unsigned char), len, out.get());
                    fwrite(&freq, sizeof(freq), 1, out.get());
                    last = doc_id;
                }
            }
        }

        // Writes doc_len_list.bin directly from (doc_id, len) pairs, given
        // in increasing doc_id order.
        void write_doc_len_list(const std::vector<std::pair<unsigned long long, unsigned int>>& entries) {
            SafeFile out(doclen_dir / "doc_len_list.bin", "wb");
            unsigned long long last = 0;
            unsigned char buf[BUFFER_LIMIT];
            for (auto [doc_id, len] : entries) {
                int enc_len = vbe_encode(doc_id - last, buf);
                fwrite(buf, sizeof(unsigned char), enc_len, out.get());
                fwrite(&len, sizeof(len), 1, out.get());
                last = doc_id;
            }
        }

        fs::path doc_len_path() const {
            return doclen_dir / "doc_len_list.bin";
        }

        // Reads count postings starting at (file_index, start_addr).
        std::vector<std::pair<unsigned long long, unsigned int>> read_postings(
            unsigned int file_index, size_t start_addr, int count
        ) {
            std::vector<std::pair<unsigned long long, unsigned int>> out;
            SafeFile fp(merge_dir / std::format("posting_{:04}.bin", file_index), "rb");
            fseek(fp.get(), (long)start_addr, SEEK_SET);

            unsigned long long doc_id = 0;
            for (int k = 0; k < count; k++) {
                unsigned char buf[BUFFER_LIMIT];
                if (!read_vbe(fp.get(), buf)) break;
                doc_id += vbe_decode(buf);
                unsigned int freq;
                fread(&freq, sizeof(freq), 1, fp.get());
                out.push_back({doc_id, freq});
            }
            return out;
        }

        std::unordered_map<std::string, TermMeta> run_merge(
            float k1 = 1.2f, float b = 0.75f, int block_size = 2, size_t split_size = (1ull << 30)
        ) {
            merge_inverted_blocks(doc_len_path(), block_dir, merge_dir, k1, b, block_size, split_size);
            auto all = read_block_meta_file(merge_dir / "block_meta.bin");
            std::unordered_map<std::string, TermMeta> out;
            for (auto& [term, tm] : all) out[term] = tm;
            return out;
        }
    };

    TEST_F(MergeInvertedBlocksTest, MergesMultiShardMultiTermCorpus) {
        // Same 8-doc / 6-term / 3-shard corpus independently cross-checked
        // against a from-scratch Python reimplementation of the BM25 math
        // during development (see /data/merge_verify/gen.py). doc lengths:
        // [3,2,4,1,2,3,1,2] for docs 0..7 -> N=8, avgdl=2.25.
        write_doc_len_list({{0,3},{1,2},{2,4},{3,1},{4,2},{5,3},{6,1},{7,2}});

        write_raw_block_multi("block_0000.bin", {
            {"alpha", {{0,1}}},
            {"beta", {{0,1}}},
            {"delta", {{2,2}}},
            {"epsilon", {{2,1}}},
            {"gamma", {{0,1}}},
        });
        write_raw_block_multi("block_0001.bin", {
            {"alpha", {{1,1},{2,1},{3,1},{5,1}}},
            {"beta", {{1,1},{4,1},{5,1}}},
            {"gamma", {{4,1},{5,1},{7,1}}},
        });
        write_raw_block_multi("block_0002.bin", {
            {"alpha", {{7,1}}},
            {"zeta", {{6,1}}},
        });

        auto tm = run_merge(/*k1=*/1.2f, /*b=*/0.75f, /*block_size=*/2);

        ASSERT_EQ(tm.size(), 6);

        // doc_count / max_doc_id for every term.
        EXPECT_EQ(tm["alpha"].doc_count, 6);   EXPECT_EQ(tm["alpha"].max_doc_id, 7);
        EXPECT_EQ(tm["beta"].doc_count, 4);    EXPECT_EQ(tm["beta"].max_doc_id, 5);
        EXPECT_EQ(tm["gamma"].doc_count, 4);   EXPECT_EQ(tm["gamma"].max_doc_id, 7);
        EXPECT_EQ(tm["delta"].doc_count, 1);   EXPECT_EQ(tm["delta"].max_doc_id, 2);
        EXPECT_EQ(tm["epsilon"].doc_count, 1); EXPECT_EQ(tm["epsilon"].max_doc_id, 2);
        EXPECT_EQ(tm["zeta"].doc_count, 1);    EXPECT_EQ(tm["zeta"].max_doc_id, 6);

        // "alpha": 6 postings / block_size=2 -> 3 blocks: [0,1],[2,3],[5,7].
        // block_meta_list[i].doc_id is each block's absolute start_doc_id.
        ASSERT_EQ(tm["alpha"].block_meta_list.size(), 3);
        EXPECT_EQ(tm["alpha"].block_meta_list[0].doc_id, 0);
        EXPECT_EQ(tm["alpha"].block_meta_list[1].doc_id, 2);
        EXPECT_EQ(tm["alpha"].block_meta_list[2].doc_id, 5);
        EXPECT_NEAR(tm["alpha"].block_meta_list[0].block_ub, 0.301381f, 1e-4);
        EXPECT_NEAR(tm["alpha"].block_meta_list[1].block_ub, 0.372295f, 1e-4);
        EXPECT_NEAR(tm["alpha"].block_meta_list[2].block_ub, 0.301381f, 1e-4);
        EXPECT_NEAR(tm["alpha"].term_ub, 0.372295f, 1e-4);

        EXPECT_EQ(
            read_postings(tm["alpha"].block_meta_list[1].file_index, tm["alpha"].block_meta_list[1].start_addr, 2),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{2,1},{3,1}})
        );

        // "zeta": singleton, one partial block.
        ASSERT_EQ(tm["zeta"].block_meta_list.size(), 1);
        EXPECT_EQ(tm["zeta"].block_meta_list[0].doc_id, 6);
        EXPECT_NEAR(tm["zeta"].block_meta_list[0].block_ub, 2.691042f, 1e-3);
        EXPECT_NEAR(tm["zeta"].term_ub, 2.691042f, 1e-3);
        EXPECT_EQ(
            read_postings(tm["zeta"].block_meta_list[0].file_index, tm["zeta"].block_meta_list[0].start_addr, 1),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{6,1}})
        );
    }

    TEST_F(MergeInvertedBlocksTest, StreamsWithMultipleTermsAllGetMerged) {
        // Regression test: streams weren't being re-pushed into the term
        // heap after their first term was drained, so any stream
        // contributing more than one distinct term would silently lose
        // every term after its first. This shard has 3 terms in one
        // stream, none of which share a doc_id, so if the bug were back
        // only "aaa" would show up.
        write_doc_len_list({{1,1},{2,1},{3,1}});
        write_raw_block_multi("block_0000.bin", {
            {"aaa", {{1,1}}},
            {"bbb", {{2,1}}},
            {"ccc", {{3,1}}},
        });

        auto tm = run_merge();

        ASSERT_EQ(tm.size(), 3) << "not every term from a multi-term stream was merged";
        EXPECT_TRUE(tm.count("aaa"));
        EXPECT_TRUE(tm.count("bbb"));
        EXPECT_TRUE(tm.count("ccc"));
    }

    TEST_F(MergeInvertedBlocksTest, DuplicateDocIdSplitAcrossShardsAtBlockBoundaryMerges) {
        // A document's tokens for a term can be split across two SPIMI
        // partial blocks (build_partial_index's memory-limit check is
        // per-token, not per-document). This must merge into one posting,
        // even when the duplicate arrives exactly when the block buffer is
        // already full (block_size=2 here: doc5 then doc10's first copy
        // exactly fill the buffer before doc10's second copy arrives).
        write_doc_len_list({{5,2},{10,3}});
        write_raw_block("block_0000.bin", "omega", {{5,1},{10,3}});
        write_raw_block("block_0001.bin", "omega", {{10,2}});

        auto tm = run_merge(1.2f, 0.75f, /*block_size=*/2);

        ASSERT_EQ(tm.size(), 1);
        const TermMeta& omega = tm["omega"];
        EXPECT_EQ(omega.doc_count, 2) << "doc10 counted as two documents instead of one";
        EXPECT_EQ(omega.max_doc_id, 10);
        ASSERT_EQ(omega.block_meta_list.size(), 1);

        auto postings = read_postings(
            omega.block_meta_list[0].file_index, omega.block_meta_list[0].start_addr, 2
        );
        ASSERT_EQ(postings.size(), 2);
        EXPECT_EQ(postings[0], (std::pair<unsigned long long, unsigned int>{5, 1}));
        EXPECT_EQ(postings[1], (std::pair<unsigned long long, unsigned int>{10, 5}))
            << "doc10's freq should be merged 3+2=5, not left as two separate entries";
    }

    TEST_F(MergeInvertedBlocksTest, EmptyBlockDirProducesEmptyBlockMetaFile) {
        write_doc_len_list({{0, 1}});
        // No block_*.bin written at all.

        std::unordered_map<std::string, TermMeta> tm;
        ASSERT_NO_THROW(tm = run_merge());
        ASSERT_EQ(tm.size(), 0);
    }

    TEST_F(MergeInvertedBlocksTest, SplitSizeRoutesTermsAcrossMultiplePostingFiles) {
        write_doc_len_list({{0,1},{1,1},{2,1}});
        write_raw_block_multi("block_0000.bin", {
            {"aaa", {{0,1}}},
            {"bbb", {{1,1}}},
            {"ccc", {{2,1}}},
        });

        // The split check runs *after* build_posting_list has already
        // written the current term's data, so crossing split_size while
        // writing term N only takes effect for term N+1 - term N itself
        // still lands wherever out_file already pointed. With split_size=1,
        // "aaa" (first term, check never fires yet) and "bbb" (crosses the
        // threshold, but too late to redirect its own write) both land in
        // file 0; "ccc" is the first term guaranteed to land in file 1.
        auto tm = run_merge(1.2f, 0.75f, /*block_size=*/128, /*split_size=*/1);

        ASSERT_EQ(tm.size(), 3);
        ASSERT_EQ(tm["aaa"].block_meta_list.size(), 1);
        ASSERT_EQ(tm["bbb"].block_meta_list.size(), 1);
        ASSERT_EQ(tm["ccc"].block_meta_list.size(), 1);

        unsigned int aaa_file = tm["aaa"].block_meta_list[0].file_index;
        unsigned int bbb_file = tm["bbb"].block_meta_list[0].file_index;
        unsigned int ccc_file = tm["ccc"].block_meta_list[0].file_index;

        EXPECT_EQ(aaa_file, bbb_file) << "the term that crosses split_size still lands in the file already open";
        EXPECT_NE(bbb_file, ccc_file) << "the *next* term after crossing split_size should roll to a new file";

        EXPECT_EQ(
            read_postings(aaa_file, tm["aaa"].block_meta_list[0].start_addr, 1),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{0,1}})
        );
        EXPECT_EQ(
            read_postings(bbb_file, tm["bbb"].block_meta_list[0].start_addr, 1),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{1,1}})
        );
        EXPECT_EQ(
            read_postings(ccc_file, tm["ccc"].block_meta_list[0].start_addr, 1),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{2,1}})
        );
    }
}
