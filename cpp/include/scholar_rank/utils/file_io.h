#ifndef FILE_IO_H
#define FILE_IO_H

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

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