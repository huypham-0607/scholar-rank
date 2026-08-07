#ifndef LOGGER_H
#define LOGGER_H

#include <string>

class Logger{
public:
    enum Level {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3
    };

    Logger(std::string _file_name, Level _default_level);

    void log(std::string message) const;

    void log(std::string message, Level log_level) const;

private:
    std::string file_name;
    Level default_level;
};

#endif