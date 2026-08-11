#include "scholar_rank/utils/file_io.h"

#include <cstddef>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <format>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

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


SafeFileMmap::SafeFileMmap(fs::path _file_path) :file_path(_file_path) {
    int fd = open(file_path.string().c_str(), O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        throw std::runtime_error(std::format(
            "Error opening file {}", file_path.string()
        ));
    }

    struct stat sb;
    if (fstat(fd,&sb) == -1) {
        close(fd);
        throw std::runtime_error(std::format(
            "Error getting file size for {}", file_path.string()
        ));
    }
    if (sb.st_size == 0) {
        close(fd);
        throw std::runtime_error(std::format(
            "SafeFileMmap disallow opening empty file {}", file_path.string()
        ));
    }

    data = static_cast<unsigned char*>(
        mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0)
    );
    close(fd);

    if (data == MAP_FAILED) {
        throw std::runtime_error(std::format(
            "Error while memory-mapping file {}", file_path.string()
        ));
    }
    
}

SafeFileMmap::~SafeFileMmap() {
    if (data != NULL) {
        munmap(data, sb.st_size);
    }
}

SafeFileMmap::SafeFileMmap(SafeFileMmap&& other) noexcept: sb(other.sb), data(other.data), file_path(other.file_path) {
    other.data = nullptr;
    other.file_path = fs::path();
}

SafeFileMmap& SafeFileMmap::operator=(SafeFileMmap&& other) noexcept {
    if (this != &other) {
        if (data != nullptr) {
            munmap(data, sb.st_size);
        }
        data = other.data;
        sb = other.sb;
        file_path = other.file_path;

        other.data = nullptr;
        other.file_path = fs::path();
    }
    return *this;
}

const unsigned char SafeFileMmap::operator[](size_t idx) const {
    if (idx >= sb.st_size) {
        throw std::runtime_error(std::format(
            "Index {} out of bound for object SafeFileMmap.",
            idx
        ));
    }
    return data[idx];
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