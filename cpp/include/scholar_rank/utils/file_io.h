#ifndef FILE_IO_H
#define FILE_IO_H

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

/**
 * @brief C style FileIO wrapper for safe file descriptor handling.
 * 
 */
class SafeFile{
public:
    SafeFile(const fs::path& path, const char* mode);
    SafeFile(const char* const path, const char* mode);
    ~SafeFile();

    SafeFile(const SafeFile&) = delete;
    SafeFile& operator=(const SafeFile&) = delete;
    SafeFile(SafeFile&& other) noexcept;
    SafeFile& operator=(SafeFile&& other) noexcept;

    FILE* get() const noexcept;
    void close();
private:
    FILE* fp;
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