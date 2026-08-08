#include "scholar_rank/utils/file_io.h"

#include <random>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <unistd.h>
#include <fstream>
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

class GlobFilesTest : public testing::Test {
protected:

    void SetUp() override {
        tmp_path = makeUniqueTempDir();

    }

    void TearDown() override {
        fs::remove_all(tmp_path);
    }

    fs::path tmp_path;
    std::vector<std::string> ext = {
        ".txt",
        ".csv",
        ".json",
        ".parquet",
        ".bin"
    };
    std::vector<std::string> pref = {
        "miku",
        "teto",
        "reimu",
        "marisa",
        "beanstalk"
    };
};

TEST_F(GlobFilesTest, NoMatchExt) {
    int file_counts = 10;
    std::string chosen_ext = ".bin";
    std::string mismatched_ext = ".txt";

    for (int i = 0; i < file_counts; i++) {

        fs::path file_path = tmp_path / (std::to_string(i) + chosen_ext);

        std::ofstream file(file_path, std::ios::trunc);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to create test files.");
        }
    }

    std::vector<fs::path> expected = std::vector<fs::path>();
    std::vector<fs::path> query = glob_files(tmp_path, "", mismatched_ext);

    ASSERT_EQ(query, expected);
}

TEST_F(GlobFilesTest, NoMatchPref) {
    int file_counts = 10;
    std::string chosen_ext = ".bin";

    for (int i = 0; i < file_counts; i++) {

        fs::path file_path = tmp_path / (std::to_string(i) + chosen_ext);

        std::ofstream file(file_path, std::ios::trunc);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to create test files.");
        }
    }

    std::vector<fs::path> expected = std::vector<fs::path>();
    std::vector<fs::path> query = glob_files(tmp_path, "impossible_match", chosen_ext);

    ASSERT_EQ(query, expected);
}

TEST_F(GlobFilesTest, MissingDot) {
    int file_counts = 10;
    std::string chosen_ext = ".bin";
    std::string mismatched_ext = "bin";

    for (int i = 0; i < file_counts; i++) {

        fs::path file_path = tmp_path / (std::to_string(i) + chosen_ext);

        std::ofstream file(file_path, std::ios::trunc);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to create test files.");
        }
    }

    std::vector<fs::path> expected = std::vector<fs::path>();
    std::vector<fs::path> query = glob_files(tmp_path, "", mismatched_ext);

    ASSERT_EQ(query, expected);
}

TEST_F(GlobFilesTest, RandomNoPrefix) {
    std:: mt19937_64 mt(42);

    unsigned long long ext_l = 0;
    unsigned long long ext_r = ext.size()-1;
    
    int n = 1000;
    int file_counts = 20;

    for (int iter = 0; iter < n; iter++){

        std::vector<std::vector<fs::path>> expected(ext.size(),std::vector<fs::path>());

        for (int i = 0; i < file_counts; i++) {
            int ext_id = rd(ext_l, ext_r, mt);

            fs::path file_path = tmp_path / (
                std::format("{:05}{}", i, ext[ext_id])
            );

            std::ofstream file(file_path, std::ios::trunc);

            if (!file.is_open()) {
                throw std::runtime_error("Failed to create test files.");
            }

            expected[ext_id].push_back(file_path);
        }

        for (int i = 0; i < ext.size(); i++){
            std::vector<fs::path> query = glob_files(tmp_path, "", ext[i]);

            ASSERT_EQ(query, expected[i]);
        }

        for (const auto& entry : fs::directory_iterator(tmp_path)) {
            if (entry.is_regular_file()) {
                fs::remove(entry.path());
            }
        }
    }
}

TEST_F(GlobFilesTest, RandomWithPrefix) {
    std:: mt19937_64 mt(42);

    unsigned long long ext_l = 0;
    unsigned long long ext_r = ext.size()-1;
    unsigned long long pref_l = 0;
    unsigned long long pref_r = pref.size()-1;    

    int n = 1000;
    int file_counts = 20;

    for (int iter = 0; iter < n; iter++){
        std::vector<std::vector<std::vector<fs::path>>> expected(
            ext.size(), std::vector<std::vector<fs::path>>(
                pref.size(), std::vector<fs::path>()
            )
        );

        for (int i = 0; i < file_counts; i++) {
            int ext_id = rd(ext_l, ext_r, mt);
            int pref_id = rd(pref_l, pref_r, mt);

            fs::path file_path = tmp_path / (
                std::format("{}_{:05}{}", pref[pref_id], i, ext[ext_id])
            );

            std::ofstream file(file_path, std::ios::trunc);

            if (!file.is_open()) {
                throw std::runtime_error("Failed to create test files.");
            }

            expected[ext_id][pref_id].push_back(file_path);
        }

        for (int i = 0; i < ext.size(); i++){
            for (int j = 0; j < pref.size(); j++){
                std::vector<fs::path> query = glob_files(tmp_path, pref[j], ext[i]);

                ASSERT_EQ(query, expected[i][j]);
            }
        }
        for (const auto& entry : fs::directory_iterator(tmp_path)) {
            if (entry.is_regular_file()) {
                fs::remove(entry.path());
            }
        }
    }
}