#include "scholar_rank/utils/logger.hpp"

#include <string>
#include <chrono>
#include <format>
#include <iostream>

namespace {
    constexpr std::string_view level_name(Logger::Level level) {
        switch (level) {
            case Logger::Level::DEBUG:   return "DEBUG";
            case Logger::Level::INFO:    return "INFO";
            case Logger::Level::WARNING: return "WARNING";
            case Logger::Level::ERROR:   return "ERROR";
        }
        return "UNKNOWN";
    }
}

Logger::Logger(std::string _file_name, Level _default_level = DEBUG) :
    file_name(_file_name),
    default_level(_default_level) {};

void Logger::log(std::string message){

}

void Logger::log(std::string message, Level log_level) {
    std::chrono::time_point now = std::chrono::system_clock::now();
    std::chrono::zoned_time local_time{std::chrono::current_zone(), now};
    auto zone = local_time.get_time_zone()->get_info(now).abbrev;
    std::cerr << std::format("{0} {1} [{2}] {3}: {4}\n", local_time, zone, level_name(log_level), file_name, message);
}