#include "scholar_rank/utils/file_io.h"

#include <random>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <unistd.h>
#include <fstream>
#include <format>
#include <cstdio>
#include <utility>

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

namespace SafeFileTest {
    class SafeFileTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;
    };

    TEST_F(SafeFileTest, ConstructWriteThenRead) {
        fs::path file_name = tmp_path / "safe_file.bin";
        std::string payload = "hello safefile";

        {
            SafeFile out_fp(file_name, "wb");
            ASSERT_NE(out_fp.get(), nullptr);
            size_t written = fwrite(payload.c_str(), sizeof(char), payload.size(), out_fp.get());
            ASSERT_EQ(written, payload.size());
        }

        SafeFile in_fp(file_name, "rb");
        std::string read_back(payload.size(), '\0');
        size_t got = fread(&read_back[0], sizeof(char), payload.size(), in_fp.get());
        ASSERT_EQ(got, payload.size());
        ASSERT_EQ(read_back, payload);
    }

    TEST_F(SafeFileTest, ConstCharOverload) {
        fs::path file_name = tmp_path / "safe_file.bin";

        {
            SafeFile out_fp(file_name.string().c_str(), "wb");
            ASSERT_NE(out_fp.get(), nullptr);
        }

        SafeFile in_fp(file_name.string().c_str(), "rb");
        ASSERT_NE(in_fp.get(), nullptr);
    }

    TEST_F(SafeFileTest, ThrowsOnMissingFile) {
        fs::path file_name = tmp_path / "does_not_exist.bin";
        ASSERT_THROW(SafeFile in_fp(file_name, "rb"), std::runtime_error);
    }

    TEST_F(SafeFileTest, ThrowsOnMissingParentDir) {
        fs::path file_name = tmp_path / "missing_dir" / "safe_file.bin";
        ASSERT_THROW(SafeFile out_fp(file_name, "wb"), std::runtime_error);
    }

    TEST_F(SafeFileTest, MoveConstructor) {
        fs::path file_name = tmp_path / "safe_file.bin";
        SafeFile original(file_name, "wb");
        FILE* raw_fp = original.get();
        ASSERT_NE(raw_fp, nullptr);

        SafeFile moved(std::move(original));
        ASSERT_EQ(moved.get(), raw_fp);
        ASSERT_EQ(original.get(), nullptr);

        unsigned char byte = 42;
        size_t written = fwrite(&byte, sizeof(byte), 1, moved.get());
        ASSERT_EQ(written, 1);
    }

    TEST_F(SafeFileTest, MoveAssignment) {
        fs::path file_a = tmp_path / "a.bin";
        fs::path file_b = tmp_path / "b.bin";

        SafeFile dest(file_a, "wb");
        {
            SafeFile source(file_b, "wb");
            dest = std::move(source);
            ASSERT_EQ(source.get(), nullptr);
        }

        unsigned char byte = 7;
        size_t written = fwrite(&byte, sizeof(byte), 1, dest.get());
        ASSERT_EQ(written, 1);
        dest.close();

        // file_a (the previous handle) was closed by the move-assignment
        // before anything was ever written to it, so it stays empty; the
        // byte we wrote afterward should have landed in file_b instead.
        ASSERT_EQ(fs::file_size(file_a), 0u);
        ASSERT_EQ(fs::file_size(file_b), 1u);
    }

    TEST_F(SafeFileTest, CloseIsIdempotent) {
        fs::path file_name = tmp_path / "safe_file.bin";
        SafeFile fp(file_name, "wb");

        ASSERT_NO_THROW(fp.close());
        ASSERT_EQ(fp.get(), nullptr);
        ASSERT_NO_THROW(fp.close());
    }

    TEST_F(SafeFileTest, DestructorFlushesAndCloses) {
        fs::path file_name = tmp_path / "safe_file.bin";
        std::string payload = "flushed on scope exit";

        {
            SafeFile fp(file_name, "wb");
            fwrite(payload.c_str(), sizeof(char), payload.size(), fp.get());
        } // destructor runs here; a leaked/unflushed handle would fail the read below

        SafeFile check(file_name, "rb");
        std::string read_back(payload.size(), '\0');
        size_t got = fread(&read_back[0], sizeof(char), payload.size(), check.get());
        ASSERT_EQ(got, payload.size());
        ASSERT_EQ(read_back, payload);
    }
}

namespace SafeFileMmapTest {
    class SafeFileMmapTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;

        void write_file(const fs::path& path, const std::string& content) {
            SafeFile out_fp(path, "wb");
            if (!content.empty()) {
                size_t written = fwrite(content.data(), sizeof(char), content.size(), out_fp.get());
                if (written != content.size()) {
                    throw std::runtime_error("Failed to write test fixture file.");
                }
            }
        }
    };

    TEST_F(SafeFileMmapTest, ConstructsAndReadsAllBytes) {
        fs::path file_name = tmp_path / "mmap_basic.bin";
        std::string payload = "hello mmap";
        write_file(file_name, payload);

        SafeFileMmap fp(file_name);
        for (size_t i = 0; i < payload.size(); i++) {
            EXPECT_EQ(fp[i], (unsigned char)payload[i]);
        }
    }

    TEST_F(SafeFileMmapTest, ReadsContentSpanningMultiplePages) {
        // Larger than a typical 4KB page, to catch any indexing bug that
        // only shows up once the mapping spans multiple pages.
        std::mt19937_64 mt(1234);
        std::string content(10000, '\0');
        for (char& c : content) {
            c = static_cast<char>(rd(0, 255, mt));
        }

        fs::path file_name = tmp_path / "mmap_large.bin";
        write_file(file_name, content);

        SafeFileMmap fp(file_name);
        for (size_t i = 0; i < content.size(); i++) {
            ASSERT_EQ(fp[i], (unsigned char)content[i]) << "mismatch at index " << i;
        }
    }

    TEST_F(SafeFileMmapTest, ThrowsOnMissingFile) {
        fs::path file_name = tmp_path / "does_not_exist.bin";
        ASSERT_THROW(SafeFileMmap fp(file_name), std::runtime_error);
    }

    TEST_F(SafeFileMmapTest, ThrowsOnEmptyFile) {
        fs::path file_name = tmp_path / "empty.bin";
        write_file(file_name, "");
        ASSERT_THROW(SafeFileMmap fp(file_name), std::runtime_error);
    }

    TEST_F(SafeFileMmapTest, OutOfBoundsAccessThrows) {
        fs::path file_name = tmp_path / "mmap_bounds.bin";
        write_file(file_name, "abc");

        SafeFileMmap fp(file_name);
        ASSERT_THROW(fp[3], std::runtime_error);
        ASSERT_THROW(fp[1000], std::runtime_error);
    }

    TEST_F(SafeFileMmapTest, LastValidIndexDoesNotThrow) {
        fs::path file_name = tmp_path / "mmap_last_idx.bin";
        std::string payload = "abcdef";
        write_file(file_name, payload);

        SafeFileMmap fp(file_name);
        unsigned char last = 0;
        ASSERT_NO_THROW(last = fp[payload.size() - 1]);
        EXPECT_EQ(last, (unsigned char)'f');
    }

    TEST_F(SafeFileMmapTest, MoveConstructorTransfersOwnership) {
        fs::path file_name = tmp_path / "mmap_move_ctor.bin";
        std::string payload = "hello mmap";
        write_file(file_name, payload);

        SafeFileMmap original(file_name);
        SafeFileMmap moved(std::move(original));

        for (size_t i = 0; i < payload.size(); i++) {
            EXPECT_EQ(moved[i], (unsigned char)payload[i]);
        }
        // `original` is moved-from and destructs at the end of this test;
        // if the move ctor failed to null out its `data`, that destructor
        // would double-munmap the mapping `moved` still owns.
    }

    TEST_F(SafeFileMmapTest, MoveAssignmentTransfersOwnership) {
        fs::path file_a = tmp_path / "a.bin";
        fs::path file_b = tmp_path / "b.bin";
        write_file(file_a, "AAAA");
        write_file(file_b, "BBBBBB");

        SafeFileMmap dest(file_a);
        EXPECT_EQ(dest[0], (unsigned char)'A');
        {
            SafeFileMmap source(file_b);
            dest = std::move(source);
            // source is moved-from and destructs at the end of this block -
            // must not touch the mapping dest now owns.
        }

        ASSERT_EQ(dest[0], (unsigned char)'B');
        for (size_t i = 0; i < 6; i++) {
            EXPECT_EQ(dest[i], (unsigned char)"BBBBBB"[i]);
        }
    }

    TEST_F(SafeFileMmapTest, DestructorReleasesMappingCleanly) {
        fs::path file_name = tmp_path / "mmap_dtor.bin";
        write_file(file_name, "destructor test payload");

        {
            SafeFileMmap fp(file_name);
            EXPECT_EQ(fp[0], (unsigned char)'d');
        } // destructor runs here

        // File should still be intact and independently removable - proves
        // the mapping (and the fd, already closed in the constructor) isn't
        // held open past scope exit.
        ASSERT_TRUE(fs::exists(file_name));
        ASSERT_NO_THROW(fs::remove(file_name));
    }
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