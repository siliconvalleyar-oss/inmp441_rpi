#include "core/Logger.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace core {

namespace {
constexpr int kMaxMessageLength = 512;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarning:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
    }
    return "?";
}
}  // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::kDebug, fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::kInfo, fmt, args);
    va_end(args);
}

void Logger::warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::kWarning, fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::kError, fmt, args);
    va_end(args);
}

void Logger::log(LogLevel level, const char* fmt, va_list args) {
    static std::mutex mutex;

    if (level < level_) {
        return;
    }

    std::lock_guard<std::mutex> guard(mutex);

    std::string prefix;
    if (timestamps_) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char stamp[32] = {0};
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
        prefix += stamp;
        prefix += " ";
    }
    prefix += "[";
    prefix += levelName(level);
    prefix += "] ";

    char buffer[kMaxMessageLength] = {0};
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::fprintf(stderr, "%s%s\n", prefix.c_str(), buffer);
    std::fflush(stderr);
}

}  // namespace core
