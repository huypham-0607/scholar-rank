#include "scholar_rank/utils/file_io.h"

#include <filesystem>
#include <vector>
#include <algorithm>
#include <format>

namespace fs = std::filesystem;

SafeFile::SafeFile(const fs::path& path, const char* mode) 
    : fp(std::fopen(path.string().c_str(), mode)) {
    if (fp == nullptr) {
        throw std::runtime_error(
            std::format("Failed to open file {}.", path.string())
        );
    }
}

SafeFile::SafeFile(const char* const path, const char* mode) 
    : fp(std::fopen(path, mode)) {
    if (fp == nullptr) {
        throw std::runtime_error(
            std::format("Failed to open file {}.", path)
        );
    }
}

SafeFile::~SafeFile() {
    if (fp != nullptr) {
        std::fclose(fp);
    }
}

SafeFile::SafeFile(SafeFile&& other) noexcept : fp(other.fp) {
    other.fp = nullptr;
}

SafeFile& SafeFile::operator=(SafeFile&& other) noexcept {
    if (this != &other) {
        if (fp != nullptr) {
            std::fclose(fp);
        }
        fp = other.fp;
        other.fp = nullptr;
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
        throw std::runtime_error("Failed to close file.");
    }
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