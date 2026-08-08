#include "scholar_rank/retrieval/index_builder.h"

#include <random>
#include <filesystem>
#include <gtest/gtest.h>
#include <cstdio>
#include <stdexcept>
#include <format>

namespace fs = std::filesystem;

unsigned long long rd(unsigned long long l, unsigned long long r, std::mt19937_64 &mt) {
    return std::uniform_int_distribution<unsigned long long> (l,r) (mt);
}

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

namespace ReadTokenTest{
    class ReadTokenTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;
    };

    TEST_F(ReadTokenTest, ValidInput) {
        std::vector<std::pair<unsigned long long, std::string>> v;
        v.push_back({6767, "sixseven"});
        v.push_back({2000, "y2k"});
        v.push_back({33, "maxverstappen"});
        v.push_back({(1LL<<60),"bignumber"});

        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");

        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );
        
        for (int i = 0; i < v.size(); i++){
            fwrite(&v[i].first, sizeof(v[i].first), 1, file_fp);
            unsigned short term_length = v[i].second.size();
            fwrite(&term_length, sizeof(term_length), 1, file_fp);
            fwrite(v[i].second.c_str(), sizeof(char), term_length, file_fp);
        }

        fclose(file_fp);
        file_fp = std::fopen(file_name.string().c_str(), "rb");

        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;

        for (int i = 0; i < v.size(); i++){
            bool res;
            ASSERT_NO_THROW(res = read_token(file_fp, &doc_id, &buffer));

            ASSERT_TRUE(res);
            ASSERT_EQ(doc_id, v[i].first);
            ASSERT_EQ(buffer, v[i].second);
        }

        bool res;

        ASSERT_NO_THROW(res = read_token(file_fp, &doc_id, &buffer));
        ASSERT_FALSE(res) << "doc_id=" << doc_id << " buffer=" << buffer;
    }

    TEST_F(ReadTokenTest, EmptyStream) {
        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        fclose(file_fp);
        
        file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_NO_THROW(res = read_token(file_fp, &doc_id, &buffer));
        ASSERT_FALSE(res) << "doc_id=" << doc_id << " buffer=" << buffer;
    }

    TEST_F(ReadTokenTest, PartialDocId) {
        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned int doc_id_4_bytes = 177013;
        fwrite(&doc_id_4_bytes, sizeof(doc_id_4_bytes), 1, file_fp);

        fclose(file_fp);
        
        file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_THROW(res = read_token(file_fp, &doc_id, &buffer), std::runtime_error);
    }

    TEST_F(ReadTokenTest, PartialTermLength) {
        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long valid_doc_id = 177013;
        unsigned char term_length_1_bytes = 10;
        fwrite(&valid_doc_id, sizeof(valid_doc_id), 1, file_fp);
        fwrite(&term_length_1_bytes, sizeof(term_length_1_bytes), 1, file_fp);

        fclose(file_fp);
        
        file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_THROW(res = read_token(file_fp, &doc_id, &buffer), std::runtime_error);
    }

    TEST_F(ReadTokenTest, InvalidTermLength) {
        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long valid_doc_id = 177013;
        unsigned short invalid_term_length = (1<<10);
        fwrite(&valid_doc_id, sizeof(valid_doc_id), 1, file_fp);
        fwrite(&invalid_term_length, sizeof(invalid_term_length), 1, file_fp);

        fclose(file_fp);
        
        file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_THROW(res = read_token(file_fp, &doc_id, &buffer), std::runtime_error);
    }

    TEST_F(ReadTokenTest, PartialTermValue) {
        fs::path file_name = tmp_path / "token_stream.bin";

        FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long valid_doc_id = 177013;
        std::string term_value = "catmemes";
        unsigned short valid_term_length = term_value.size();
        fwrite(&valid_doc_id, sizeof(valid_doc_id), 1, file_fp);
        fwrite(&valid_term_length, sizeof(valid_term_length), 1, file_fp);
        fwrite(term_value.c_str(), sizeof(char), valid_term_length - 1, file_fp);

        fclose(file_fp);
        
        file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_THROW(res = read_token(file_fp, &doc_id, &buffer), std::runtime_error);
    }

    TEST_F(ReadTokenTest, NullFilePointer) {
        fs::path file_name = tmp_path / "token_stream.bin";
        
        FILE* file_fp = std::fopen(file_name.string().c_str(), "rb");

        unsigned long long doc_id;
        std::string buffer;
        bool res;

        ASSERT_THROW(res = read_token(file_fp, &doc_id, &buffer), std::runtime_error);
    }

    // TODO: Add Randomized Stress Test to ReadTokenTest
}

namespace BuildPartialIndexTest {
    class BuildPartialIndexTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;

        void create_stream(
            const std::vector<std::pair<unsigned long long, std::string>>& v,
            fs::path file_name
        ) {
            FILE* file_fp = std::fopen(file_name.string().c_str(), "wb");

            if (file_fp == NULL) throw std::runtime_error(
                std::format("Failed to open file {}.", file_name.string())
            );

            for (int i = 0; i < v.size(); i++){
                fwrite(&v[i].first, sizeof(v[i].first), 1, file_fp);
                unsigned short term_length = v[i].second.size();
                fwrite(&term_length, sizeof(term_length), 1, file_fp);
                fwrite(v[i].second.c_str(), sizeof(char), term_length, file_fp);
            }

            fclose(file_fp);
        }
    };

    TEST_F(BuildPartialIndexTest, ValidDistinctInput) {
        // Fabricate data
        std::vector<std::pair<unsigned long long, std::string>> v;
        v.push_back({33, "maxverstappen"});
        v.push_back({2000, "y2k"});
        v.push_back({6767, "sixseven"});
        v.push_back({(1LL<<60),"bignumber"});

        sort(v.begin(),v.end());

        fs::path file_name = tmp_path / "token_stream.bin";

        // Create stream at file_name
        create_stream(v, file_name);
        
        FILE* file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        std::unordered_map<std::string, PostingList> posting_list_mapping;
        std::vector<std::string> dictionary;

        bool res;

        ASSERT_NO_THROW(
            res = build_partial_index(
                file_fp,
                (size_t)2*(1LL<<20),
                posting_list_mapping,
                dictionary
            )
        );

        ASSERT_TRUE(res);

        sort(v.begin(), v.end(), [&](auto x, auto y) {
            return x.second < y.second;
        });

        ASSERT_EQ(dictionary.size(), v.size());
        for (int i = 0; i < v.size(); i++){
            ASSERT_EQ(dictionary[i], v[i].second);
        }

        ASSERT_EQ(posting_list_mapping.size(), v.size());

        for (int i = 0; i < v.size(); i++){
            ASSERT_TRUE(
                posting_list_mapping.find(v[i].second) != posting_list_mapping.end()
            );

            ASSERT_TRUE(
                posting_list_mapping[v[i].second][0].doc_id == v[i].first
            );

            ASSERT_TRUE(
                posting_list_mapping[v[i].second][0].freq == 1 
            );
        }
    }

    TEST_F(BuildPartialIndexTest, ValidDuplicateInput) {
        // Fabricate data
        std::vector<std::pair<unsigned long long, std::string>> v;
        v.push_back({33, "maxverstappen"});
        v.push_back({33, "maxverstappen"});
        v.push_back({33, "lewishamilton"});
        v.push_back({2000, "y2k"});
        v.push_back({2000, "y2k"});
        v.push_back({2000, "y2k"});
        v.push_back({2000, "k2y"});
        v.push_back({(1LL<<60),"bignumber"});

        sort(v.begin(),v.end());

        fs::path file_name = tmp_path / "token_stream.bin";

        // Create stream at file_name
        create_stream(v, file_name);
        
        FILE* file_fp = std::fopen(file_name.string().c_str(), "rb");
        if (file_fp == NULL) throw std::runtime_error(
            std::format("Failed to open file {}.", file_name.string())
        );

        std::unordered_map<std::string, PostingList> posting_list_mapping;
        std::vector<std::string> dictionary;

        bool res;

        ASSERT_NO_THROW(
            res = build_partial_index(
                file_fp,
                (size_t)2*(1LL<<20),
                posting_list_mapping,
                dictionary
            )
        );
        ASSERT_TRUE(res);

        // Setting up validation
        sort(v.begin(),v.end(), [&](auto x, auto y){
            if (x.second == y.second) {
                return x.first < y.first;
            }
            else return x.second < y.second;
        });

        // Counting frequency of doc_id : term pair
        std::map<std::pair<unsigned long long, std::string>,int> freq;
        for (auto key_value_pair : v){
            freq[key_value_pair]++;
        }

        v.resize(unique(v.begin(),v.end()) - v.begin());

        std::vector<std::string> true_dictionary;
        for (auto x : v) true_dictionary.push_back(x.second);

        // Validate dictionary.
        ASSERT_EQ(dictionary, true_dictionary);

        ASSERT_EQ(posting_list_mapping.size(), true_dictionary.size());

        // Validate posting_list_mapping
        std::string previous_term = "\n";
        unsigned long long idx = 0;
        for (const auto& key_value_pair : v){
            if (key_value_pair.second != previous_term) idx = 0;
            
            ASSERT_EQ(
                posting_list_mapping[key_value_pair.second][idx].doc_id,
                key_value_pair.first
            );

            ASSERT_EQ(
                posting_list_mapping[key_value_pair.second][idx].freq,
                freq[key_value_pair]
            );

            previous_term = key_value_pair.second;
            ++idx;
        }
    }
}