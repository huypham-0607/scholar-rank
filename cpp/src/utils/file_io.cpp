#include "scholar_rank/utils/file_io.h"

#include <filesystem>
#include <vector>
#include <algorithm>
#include <format>

namespace fs = std::filesystem;

SafeFile::SafeFile(const fs::path& path, const char* mode)
    : fp(std::fopen(path.string().c_str(), mode)), file_path(path) {
    if (fp == nullptr) {
        throw std::runtime_error(
            std::format("Failed to open file {}.", path.string())
        );
    }
}

SafeFile::~SafeFile() {
    if (fp != nullptr) {
        int return_val = std::fclose(fp);
        // Maybe should log something if fclose fail.
    }
}

SafeFile::SafeFile(SafeFile&& other) noexcept : fp(other.fp), file_path(other.file_path) {
    other.fp = nullptr;
    other.file_path = fs::path();
}

SafeFile& SafeFile::operator=(SafeFile&& other) noexcept {
    if (this != &other) {
        if (fp != nullptr) {
            std::fclose(fp);
        }
        fp = other.fp;
        file_path = other.file_path;
        other.fp = nullptr;
        other.file_path = fs::path();
    }
    return *this;
}

FILE* SafeFile::get() const noexcept {
    return fp;
}

// Manual close
void SafeFile::close() {
    if (fp == nullptr) return;
    FILE* tmp = fp;
    fp = nullptr;
    if (std::fclose(tmp) != 0) {
        throw std::runtime_error(std::format(
            "Failed to close file {}.",
            file_path.string()
        ));
    }
}

bool SafeFile::fread(
    void* const buffer,
    const std::size_t size,
    const std::size_t count,
    bool throw_on_eof
) {
    long initial_pos = ftell(fp);

    std::size_t arg_count = std::fread(buffer, size, count, fp);
    if (arg_count != count) {
        long bytes_read = ftell(fp) - initial_pos;
        if ((!throw_on_eof) && bytes_read == 0) return false;
        throw std::runtime_error(std::format(
            "Failed to read file {}.",
            file_path.string()
        ));
    }
    return true;
}

bool SafeFile::fwrite(void* const buffer, const std::size_t size, const std::size_t count) {
    std::size_t arg_count = std::fwrite(buffer, size, count, fp);
    if (arg_count != count) {
        throw std::runtime_error(std::format(
            "Failed to write file {}.",
            file_path.string()
        ));
    }
    return true;
}

std::vector<fs::path> glob_files(
    const fs::path dir,
    const std::string prefix,
    const std::string extension
) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()
            && entry.path().filename().string().starts_with(prefix)
            && entry.path().extension() == extension) {
            files.push_back(entry.path());
        }
    }
    sort(files.begin(), files.end());
    return files;
}