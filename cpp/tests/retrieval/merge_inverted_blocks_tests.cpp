#include "scholar_rank/retrieval/file_names.h"
#include "scholar_rank/retrieval/merge_inverted_blocks.h"
#include "scholar_rank/utils/file_io.h"
#include "scholar_rank/utils/vbe.h"

#include <cstdio>
#include <filesystem>
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
            SafeFile out(doclen_dir / file_names::DOC_LEN_LIST, "wb");
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
            return doclen_dir / file_names::DOC_LEN_LIST;
        }

        // Reads count postings starting at (file_index, start_addr).
        std::vector<std::pair<unsigned long long, unsigned int>> read_postings(
            unsigned int file_index, size_t start_addr, int count
        ) {
            std::vector<std::pair<unsigned long long, unsigned int>> out;
            SafeFile fp(merge_dir / file_names::posting_file_name(file_index), "rb");
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

        // Reads count postings starting at (file_index, start_addr) and
        // returns the file offset immediately after the last one read - for
        // checking TermMeta.end_addr's strict-endpoint property directly,
        // rather than hand-computing expected byte offsets.
        size_t read_postings_end_addr(unsigned int file_index, size_t start_addr, int count) {
            SafeFile fp(merge_dir / file_names::posting_file_name(file_index), "rb");
            fseek(fp.get(), (long)start_addr, SEEK_SET);

            for (int k = 0; k < count; k++) {
                unsigned char buf[BUFFER_LIMIT];
                read_vbe(fp.get(), buf);
                unsigned int freq;
                fread(&freq, sizeof(freq), 1, fp.get());
            }
            return (size_t)ftell(fp.get());
        }

        std::unordered_map<std::string, TermMeta> run_merge(
            float k1 = 1.2f, float b = 0.75f, int block_size = 2, size_t split_size = (1ull << 30)
        ) {
            merge_inverted_blocks(doc_len_path(), block_dir, merge_dir, k1, b, block_size, split_size);
            auto all = read_block_meta_file(merge_dir / file_names::BLOCK_META);
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

        write_raw_block_multi(file_names::partial_block_file_name(0), {
            {"alpha", {{0,1}}},
            {"beta", {{0,1}}},
            {"delta", {{2,2}}},
            {"epsilon", {{2,1}}},
            {"gamma", {{0,1}}},
        });
        write_raw_block_multi(file_names::partial_block_file_name(1), {
            {"alpha", {{1,1},{2,1},{3,1},{5,1}}},
            {"beta", {{1,1},{4,1},{5,1}}},
            {"gamma", {{4,1},{5,1},{7,1}}},
        });
        write_raw_block_multi(file_names::partial_block_file_name(2), {
            {"alpha", {{7,1}}},
            {"zeta", {{6,1}}},
        });

        auto tm = run_merge(/*k1=*/1.2f, /*b=*/0.75f, /*block_size=*/2);

        ASSERT_EQ(tm.size(), 6);

        // doc_count for every term.
        EXPECT_EQ(tm["alpha"].doc_count, 6);
        EXPECT_EQ(tm["beta"].doc_count, 4);
        EXPECT_EQ(tm["gamma"].doc_count, 4);
        EXPECT_EQ(tm["delta"].doc_count, 1);
        EXPECT_EQ(tm["epsilon"].doc_count, 1);
        EXPECT_EQ(tm["zeta"].doc_count, 1);

        // end_addr: [block_meta_list[0].start_addr, end_addr) must hold
        // exactly this term's doc_count postings and nothing more or less
        // (strict endpoint property).
        for (const std::string& term : {"alpha", "beta", "gamma", "delta", "epsilon", "zeta"}) {
            const TermMeta& term_meta = tm[term];
            EXPECT_EQ(
                read_postings_end_addr(
                    term_meta.file_index, term_meta.block_meta_list[0].start_addr, term_meta.doc_count
                ),
                term_meta.end_addr
            ) << "term \"" << term << "\" end_addr mismatch";
        }

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
            read_postings(tm["alpha"].file_index, tm["alpha"].block_meta_list[1].start_addr, 2),
            (std::vector<std::pair<unsigned long long, unsigned int>>{{2,1},{3,1}})
        );

        // "zeta": singleton, one partial block.
        ASSERT_EQ(tm["zeta"].block_meta_list.size(), 1);
        EXPECT_EQ(tm["zeta"].block_meta_list[0].doc_id, 6);
        EXPECT_NEAR(tm["zeta"].block_meta_list[0].block_ub, 2.691042f, 1e-3);
        EXPECT_NEAR(tm["zeta"].term_ub, 2.691042f, 1e-3);
        EXPECT_EQ(
            read_postings(tm["zeta"].file_index, tm["zeta"].block_meta_list[0].start_addr, 1),
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
        write_raw_block_multi(file_names::partial_block_file_name(0), {
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
        write_raw_block(file_names::partial_block_file_name(0), "omega", {{5,1},{10,3}});
        write_raw_block(file_names::partial_block_file_name(1), "omega", {{10,2}});

        auto tm = run_merge(1.2f, 0.75f, /*block_size=*/2);

        ASSERT_EQ(tm.size(), 1);
        const TermMeta& omega = tm["omega"];
        EXPECT_EQ(omega.doc_count, 2) << "doc10 counted as two documents instead of one";
        ASSERT_EQ(omega.block_meta_list.size(), 1);

        auto postings = read_postings(
            omega.file_index, omega.block_meta_list[0].start_addr, 2
        );
        ASSERT_EQ(postings.size(), 2);
        EXPECT_EQ(postings[0], (std::pair<unsigned long long, unsigned int>{5, 1}));
        EXPECT_EQ(postings[1], (std::pair<unsigned long long, unsigned int>{10, 5}))
            << "doc10's freq should be merged 3+2=5, not left as two separate entries";

        EXPECT_EQ(
            read_postings_end_addr(omega.file_index, omega.block_meta_list[0].start_addr, omega.doc_count),
            omega.end_addr
        );
    }

    TEST_F(MergeInvertedBlocksTest, EndAddrMarksExclusiveEndOfTermsByteRange) {
        write_doc_len_list({{0,1},{1,1},{2,1}});
        write_raw_block_multi(file_names::partial_block_file_name(0), {
            {"aaa", {{0,1}}},
            {"bbb", {{1,1},{2,1}}},
        });

        // block_size=128 comfortably exceeds either term's posting count,
        // so each term lands in exactly one block, both in the same file
        // (posting_0000.bin), written back to back: "aaa" first, "bbb"
        // immediately after.
        auto tm = run_merge(1.2f, 0.75f, /*block_size=*/128);

        ASSERT_EQ(tm["aaa"].block_meta_list.size(), 1);
        ASSERT_EQ(tm["bbb"].block_meta_list.size(), 1);

        // Delta-encoding resets to 0 at each block's first item, so "aaa"'s
        // sole posting (doc_id=0) VBE-encodes its delta (0) in 1 byte, plus
        // a 4-byte freq = 5 bytes total. "bbb" starts immediately after.
        EXPECT_EQ(tm["aaa"].block_meta_list[0].start_addr, 0u);
        EXPECT_EQ(tm["aaa"].end_addr, 5u);
        EXPECT_EQ(tm["bbb"].block_meta_list[0].start_addr, 5u);

        // General property, not tied to the hand-verified byte count above:
        // reading exactly doc_count postings from the first block's
        // start_addr must land the cursor exactly on end_addr - no gap,
        // no overlap into whatever comes next.
        for (const std::string& term : {"aaa", "bbb"}) {
            const TermMeta& term_meta = tm[term];
            EXPECT_EQ(
                read_postings_end_addr(
                    term_meta.file_index, term_meta.block_meta_list[0].start_addr, term_meta.doc_count
                ),
                term_meta.end_addr
            ) << "term \"" << term << "\" end_addr mismatch";
        }
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
        write_raw_block_multi(file_names::partial_block_file_name(0), {
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

        unsigned int aaa_file = tm["aaa"].file_index;
        unsigned int bbb_file = tm["bbb"].file_index;
        unsigned int ccc_file = tm["ccc"].file_index;

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

    TEST_F(MergeInvertedBlocksTest, MergeInvertedBlocksWritesMetadataFile) {
        write_doc_len_list({{0,1},{1,1}});
        write_raw_block_multi(file_names::partial_block_file_name(0), {
            {"aaa", {{0,1}}},
            {"bbb", {{1,1}}},
        });

        merge_inverted_blocks(doc_len_path(), block_dir, merge_dir, /*k1=*/1.3f, /*b=*/0.6f, /*block_size=*/4, /*split_size=*/(1ull << 20));

        // write_metadata writes both a human-readable .txt and the
        // authoritative .bin twin read_metadata actually parses.
        ASSERT_TRUE(fs::exists(merge_dir / file_names::METADATA_TXT));
        ASSERT_TRUE(fs::exists(merge_dir / file_names::METADATA_BIN));

        fs::path posting_dir, doc_len_dir;
        float k1, b;
        int block_size;
        size_t split_size;
        ASSERT_NO_THROW(read_metadata(merge_dir / file_names::METADATA_BIN, posting_dir, doc_len_dir, k1, b, block_size, split_size));

        EXPECT_EQ(posting_dir, merge_dir);
        EXPECT_EQ(doc_len_dir, doc_len_path());
        EXPECT_EQ(k1, 1.3f);
        EXPECT_EQ(b, 0.6f);
        EXPECT_EQ(block_size, 4);
        EXPECT_EQ(split_size, (1ull << 20));
    }
}

namespace MetadataTest {
    class MetadataTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;
    };

    TEST_F(MetadataTest, WriteThenReadRoundTrips) {
        fs::path txt_path = tmp_path / file_names::METADATA_TXT;
        fs::path bin_path = tmp_path / file_names::METADATA_BIN;
        fs::path posting_dir = tmp_path / "posting";
        fs::path doc_len_dir = tmp_path / "doclen";

        write_metadata(txt_path, posting_dir, doc_len_dir, 1.2f, 0.75f, 128, (1ull << 30));
        ASSERT_TRUE(fs::exists(bin_path));

        fs::path read_posting_dir, read_doc_len_dir;
        float k1, b;
        int block_size;
        size_t split_size;
        read_metadata(bin_path, read_posting_dir, read_doc_len_dir, k1, b, block_size, split_size);

        EXPECT_EQ(read_posting_dir, posting_dir);
        EXPECT_EQ(read_doc_len_dir, doc_len_dir);
        // Binary storage is exact, unlike the text copy's "%f" formatting
        // (see write_metadata's doc comment) - no tolerance needed.
        EXPECT_EQ(k1, 1.2f);
        EXPECT_EQ(b, 0.75f);
        EXPECT_EQ(block_size, 128);
        EXPECT_EQ(split_size, (1ull << 30));
    }

    TEST_F(MetadataTest, WriteThenReadRoundTripsWithDifferentValues) {
        // Regression guard against field-order/field-name mixups (eg. the
        // earlier split_size/block_size key collision), and against values
        // that would NOT survive "%f" round-tripping (1.69161642f is one of
        // the mismatching values found while investigating the text
        // format's precision loss) - proves the binary path is unaffected.
        fs::path txt_path = tmp_path / file_names::METADATA_TXT;
        fs::path bin_path = tmp_path / file_names::METADATA_BIN;
        fs::path posting_dir = tmp_path / "custom_posting_dir";
        fs::path doc_len_dir = tmp_path / "custom_doclen_dir";
        float k1 = 1.69161642f;
        float b = 0.30673251f;

        write_metadata(txt_path, posting_dir, doc_len_dir, k1, b, 256, 12345678ull);

        fs::path read_posting_dir, read_doc_len_dir;
        float read_k1, read_b;
        int block_size;
        size_t split_size;
        read_metadata(bin_path, read_posting_dir, read_doc_len_dir, read_k1, read_b, block_size, split_size);

        EXPECT_EQ(read_posting_dir, posting_dir);
        EXPECT_EQ(read_doc_len_dir, doc_len_dir);
        EXPECT_EQ(read_k1, k1);
        EXPECT_EQ(read_b, b);
        EXPECT_EQ(block_size, 256);
        EXPECT_EQ(split_size, 12345678ull);
    }

    TEST_F(MetadataTest, WriteMetadataAlsoWritesHumanReadableTextFile) {
        fs::path txt_path = tmp_path / file_names::METADATA_TXT;

        write_metadata(txt_path, tmp_path / "posting", tmp_path / "doclen", 1.2f, 0.75f, 128, (1ull << 30));
        ASSERT_TRUE(fs::exists(txt_path));

        SafeFile in(txt_path, "r");
        std::string content;
        char buf[256];
        while (std::fgets(buf, sizeof(buf), in.get()) != nullptr) {
            content += buf;
        }

        EXPECT_NE(content.find("posting_dir="), std::string::npos);
        EXPECT_NE(content.find("doc_len_dir="), std::string::npos);
        EXPECT_NE(content.find("k1="), std::string::npos);
        EXPECT_NE(content.find("b="), std::string::npos);
        EXPECT_NE(content.find("block_size="), std::string::npos);
        EXPECT_NE(content.find("split_size="), std::string::npos);
    }

    TEST_F(MetadataTest, ReadThrowsOnMissingFile) {
        fs::path bin_path = tmp_path / "does_not_exist.bin";
        fs::path posting_dir, doc_len_dir;
        float k1, b;
        int block_size;
        size_t split_size;
        ASSERT_THROW(
            read_metadata(bin_path, posting_dir, doc_len_dir, k1, b, block_size, split_size),
            std::runtime_error
        );
    }

    TEST_F(MetadataTest, ReadThrowsOnFileTruncatedBeforeTrailingField) {
        fs::path txt_path = tmp_path / file_names::METADATA_TXT;
        fs::path bin_path = tmp_path / file_names::METADATA_BIN;
        write_metadata(txt_path, tmp_path / "posting", tmp_path / "doclen", 1.2f, 0.75f, 128, (1ull << 30));

        // Every field up through block_size is intact; split_size (an
        // 8-byte trailing field) is cut short.
        auto full_size = fs::file_size(bin_path);
        std::error_code ec;
        fs::resize_file(bin_path, full_size - 4, ec);
        ASSERT_FALSE(ec);

        fs::path posting_dir, doc_len_dir;
        float k1, b;
        int block_size;
        size_t split_size;
        ASSERT_THROW(
            read_metadata(bin_path, posting_dir, doc_len_dir, k1, b, block_size, split_size),
            std::runtime_error
        );
    }

    TEST_F(MetadataTest, ReadThrowsOnFileTruncatedMidString) {
        fs::path txt_path = tmp_path / file_names::METADATA_TXT;
        fs::path bin_path = tmp_path / file_names::METADATA_BIN;
        write_metadata(txt_path, tmp_path / "posting", tmp_path / "doclen", 1.2f, 0.75f, 128, (1ull << 30));

        // 1 byte is shorter than even posting_dir's 2-byte length prefix.
        std::error_code ec;
        fs::resize_file(bin_path, 1, ec);
        ASSERT_FALSE(ec);

        fs::path posting_dir, doc_len_dir;
        float k1, b;
        int block_size;
        size_t split_size;
        ASSERT_THROW(
            read_metadata(bin_path, posting_dir, doc_len_dir, k1, b, block_size, split_size),
            std::runtime_error
        );
    }
}
