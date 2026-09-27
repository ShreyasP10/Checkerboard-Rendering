#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iostream>

namespace cbr {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Get();

    void Initialize(const std::string& logFilePath);
    void Shutdown();

    void Log(LogLevel level, const std::string& message);

    template<typename... Args>
    void LogFmt(LogLevel level, const char* format, Args... args) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), format, args...);
        Log(level, std::string(buffer));
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream m_logFile;
    std::mutex    m_mutex;
    bool          m_initialized{ false };
};

} // namespace cbr

#define CBR_LOG_DEBUG(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define CBR_LOG_INFO(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Info, fmt, ##__VA_ARGS__)
#define CBR_LOG_WARN(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define CBR_LOG_ERROR(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Error, fmt, ##__VA_ARGS__)
