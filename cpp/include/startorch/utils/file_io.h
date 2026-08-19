#ifndef FILE_IO_H
#define FILE_IO_H

#include <filesystem>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

namespace fs = std::filesystem;

/**
 * @brief C style FileIO wrapper for safe file descriptor handling.
 * 
 */

const size_t MIN_BUF_SIZE = (1<<16);

class SafeFile{
public:
    SafeFile(const fs::path& path, const char* mode);
    ~SafeFile();

    SafeFile(const SafeFile&) = delete;
    SafeFile& operator=(const SafeFile&) = delete;
    SafeFile(SafeFile&& other) noexcept;
    SafeFile& operator=(SafeFile&& other) noexcept;

    FILE* get() const noexcept;
    bool fread(
        void* const buffer,
        const std::size_t size,
        const std::size_t count,
        const bool throw_on_eof = true
    );
    bool fwrite(
        void* const buffer,
        const std::size_t size,
        const std::size_t count
    );
    void close();
private:
    FILE* fp;
    fs::path file_path;
};

class BufferedWriter{
public:
    BufferedWriter(const fs::path& path, const size_t buf_size);
    ~BufferedWriter();

    BufferedWriter(const BufferedWriter&) = delete;
    BufferedWriter& operator=(const BufferedWriter&) = delete;
    BufferedWriter(BufferedWriter&& other) = delete;
    BufferedWriter& operator=(const BufferedWriter&&) = delete;

    void fwrite(const void* const buffer, const std::size_t size, const std::size_t count);

    const long ftell(); 
private:
    SafeFile file;
    std::vector<unsigned char> buffer;
    size_t ptr;

    void flush();
};

/**
 * @brief C style Mmap wrapper for safe file descriptor handling.
 * 
 */
class SafeFileMmap {
public:
    SafeFileMmap(fs::path _file_path);
    ~SafeFileMmap();

    SafeFileMmap(const SafeFileMmap&) = delete;
    SafeFileMmap& operator=(const SafeFileMmap&) = delete;

    SafeFileMmap(SafeFileMmap&& other) noexcept;
    SafeFileMmap& operator=(SafeFileMmap&& other) noexcept;

    unsigned char operator[](size_t idx) const;
    
private:
    struct stat sb;
    unsigned char* data;
    fs::path file_path;
};

/**
 * @brief Returns list of files in a directory with matching prefix and extension.
 * 
 * This does not search recursively.
 * 
 * @param dir Path to directory
 * @param prefix Prefix to match
 * @param extension Extension suffix to match (including the dot)
 * @return std::vector<fs::path> 
 */
std::vector<fs::path> glob_files(
    const fs::path dir,
    const std::string prefix,
    const std::string extension
);

#endif