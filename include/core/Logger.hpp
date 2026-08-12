#pragma once

#include <string>

namespace core {

// Log severity levels. Lower values are more verbose.
enum class LogLevel {
    kDebug = 0,
    kInfo = 1,
    kWarning = 2,
    kError = 3,
};

// Minimal thread-safe logger. All messages go to stderr so that stdout stays
// clean and can be used for raw data / piping.
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level) { level_ = level; }
    void setTimestampsEnabled(bool enabled) { timestamps_ = enabled; }

    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
    void warning(const char* fmt, ...);
    void error(const char* fmt, ...);

private:
    Logger() = default;

    void log(LogLevel level, const char* fmt, va_list args);

    LogLevel level_ = LogLevel::kInfo;
    bool timestamps_ = true;
};

}  // namespace core
