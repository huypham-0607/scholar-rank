#ifndef FILE_IO_HPP
#define FILE_IO_HPP

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

std::vector<fs::path> globFiles(
    const fs::path dir,
    const std::string prefix,
    const std::string extension
);

#endif