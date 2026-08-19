#include "startorch/utils/file_io.h"

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

namespace BufferedWriterTest {
    class BufferedWriterTest : public testing::Test {
    protected:

        void SetUp() override {
            tmp_path = makeUniqueTempDir();
        }

        void TearDown() override {
            fs::remove_all(tmp_path);
        }

        fs::path tmp_path;

        std::vector<unsigned char> read_all_bytes(const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::vector<unsigned char>(
                std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()
            );
        }
    };

    TEST_F(BufferedWriterTest, WritesSmallPayloadAndReadsBackAfterDestruction) {
        fs::path file_name = tmp_path / "small.bin";
        std::string payload = "hello buffered writer";

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            ASSERT_NO_THROW(writer.fwrite(payload.data(), sizeof(char), payload.size()));
        }

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes.size(), payload.size());
        for (size_t i = 0; i < payload.size(); i++) {
            EXPECT_EQ(bytes[i], (unsigned char)payload[i]);
        }
    }

    TEST_F(BufferedWriterTest, DoesNotPhysicallyWriteUntilBufferFillsOrDestructs) {
        fs::path file_name = tmp_path / "deferred.bin";
        BufferedWriter writer(file_name, MIN_BUF_SIZE);

        std::vector<unsigned char> small_chunk(100, (unsigned char)'a');
        writer.fwrite(small_chunk.data(), 1, small_chunk.size());

        // Well under buffer capacity - nothing should have hit disk yet.
        ASSERT_EQ(fs::file_size(file_name), 0u);
    }

    TEST_F(BufferedWriterTest, WriteExactlyFillingBufferDoesNotOverflowOrFlushEarly) {
        fs::path file_name = tmp_path / "exact_fill.bin";
        std::vector<unsigned char> data(MIN_BUF_SIZE);
        for (size_t i = 0; i < data.size(); i++) data[i] = (unsigned char)(i % 256);

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            ASSERT_NO_THROW(writer.fwrite(data.data(), 1, data.size()));
            // Filling to exactly capacity must not trigger a flush yet -
            // the boundary check is strictly '>', not '>='.
            ASSERT_EQ(fs::file_size(file_name), 0u);
        } // destructor flushes here

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes, data);
    }

    TEST_F(BufferedWriterTest, WriteExceedingBufferCapacityFlushesAndContinuesCorrectly) {
        fs::path file_name = tmp_path / "overflow.bin";
        size_t total = MIN_BUF_SIZE + 12345; // forces an internal flush mid-call
        std::vector<unsigned char> data(total);
        for (size_t i = 0; i < data.size(); i++) data[i] = (unsigned char)(i % 256);

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            ASSERT_NO_THROW(writer.fwrite(data.data(), 1, data.size()));
        }

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes, data);
    }

    TEST_F(BufferedWriterTest, ManySeparateSmallWritesAccumulateAcrossMultipleFlushesCorrectly) {
        fs::path file_name = tmp_path / "many_small.bin";
        // Enough individual 1-byte fwrite() calls to force at least two
        // internal flushes, one buffer-full-worth apart.
        size_t iterations = (2 * MIN_BUF_SIZE) + 777;
        std::vector<unsigned char> expected;
        expected.reserve(iterations);

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            for (size_t i = 0; i < iterations; i++) {
                unsigned char b = (unsigned char)(i % 256);
                writer.fwrite(&b, 1, 1);
                expected.push_back(b);
            }
        }

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes, expected);
    }

    TEST_F(BufferedWriterTest, SingleCallWithLargeCountForcingMidCallFlushPreservesOrder) {
        fs::path file_name = tmp_path / "midcall_flush.bin";
        // count large enough that the internal per-element loop must flush
        // at least twice within this one fwrite() call.
        size_t count = (MIN_BUF_SIZE * 2) + 500;
        std::vector<unsigned char> data(count);
        for (size_t i = 0; i < count; i++) data[i] = (unsigned char)(i % 256);

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            ASSERT_NO_THROW(writer.fwrite(data.data(), 1, count));
        }

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes, data);
    }

    TEST_F(BufferedWriterTest, TellReflectsBufferedBytesBeforeAnyFlush) {
        fs::path file_name = tmp_path / "tell_pre_flush.bin";
        BufferedWriter writer(file_name, MIN_BUF_SIZE);

        std::vector<unsigned char> chunk(500, (unsigned char)'x');
        writer.fwrite(chunk.data(), 1, chunk.size());

        // Nothing flushed to the real file yet, but tell() must still report
        // the logical position as if it had been.
        EXPECT_EQ(writer.ftell(), 500);
        EXPECT_EQ(fs::file_size(file_name), 0u);
    }

    TEST_F(BufferedWriterTest, TellReflectsPositionAcrossAFlushBoundary) {
        fs::path file_name = tmp_path / "tell_post_flush.bin";
        BufferedWriter writer(file_name, MIN_BUF_SIZE);

        std::vector<unsigned char> filler(MIN_BUF_SIZE, (unsigned char)'a');
        writer.fwrite(filler.data(), 1, filler.size());
        ASSERT_EQ(writer.ftell(), (long)MIN_BUF_SIZE);

        // One more byte forces the pending buffer to actually flush.
        unsigned char one_more = 'b';
        writer.fwrite(&one_more, 1, 1);
        EXPECT_EQ(writer.ftell(), (long)MIN_BUF_SIZE + 1);
        EXPECT_EQ(fs::file_size(file_name), MIN_BUF_SIZE);
    }

    TEST_F(BufferedWriterTest, DestructorFlushesPendingUnwrittenBuffer) {
        fs::path file_name = tmp_path / "dtor_flush.bin";
        std::string payload = "flushed only by the destructor";

        {
            BufferedWriter writer(file_name, MIN_BUF_SIZE);
            writer.fwrite(payload.data(), 1, payload.size());
            // Well under buffer capacity - nothing should be on disk yet.
            ASSERT_EQ(fs::file_size(file_name), 0u);
        } // destructor runs here; a leaked/unflushed buffer would fail the read below

        auto bytes = read_all_bytes(file_name);
        ASSERT_EQ(bytes.size(), payload.size());
        for (size_t i = 0; i < payload.size(); i++) {
            EXPECT_EQ(bytes[i], (unsigned char)payload[i]);
        }
    }

    TEST_F(BufferedWriterTest, ThrowsOnBufferSizeBelowMinimum) {
        fs::path file_name = tmp_path / "too_small.bin";
        ASSERT_THROW(BufferedWriter writer(file_name, MIN_BUF_SIZE - 1), std::runtime_error);
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