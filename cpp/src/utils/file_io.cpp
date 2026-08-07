#include "scholar_rank/utils/file_io.h"

#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

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