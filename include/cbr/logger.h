#pragma once

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

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

    // Opens the log file and flushes any messages buffered before this call.
    void Initialize(const std::filesystem::path& logFilePath);
    // Discards buffered messages and stops buffering (used when LogToFile = false).
    void Disable();
    void Shutdown();

    void SetMinLevel(LogLevel level);

    void Log(LogLevel level, const std::string& message);

    void LogFmt(LogLevel level, const char* message) {
        Log(level, std::string(message));
    }

    template<typename... Args>
    void LogFmt(LogLevel level, const char* format, Args... args) {
        // std::string / std::wstring passed through C varargs is undefined behaviour.
        static_assert((!std::is_same_v<std::decay_t<Args>, std::string> && ...),
                      "Pass std::string arguments as .c_str() to CBR_LOG_* macros");
        static_assert((!std::is_same_v<std::decay_t<Args>, std::wstring> && ...),
                      "Wide strings are not supported by CBR_LOG_* macros");
        char buffer[1024];
        const int n = std::snprintf(buffer, sizeof(buffer), format, args...);
        if (n < 0) return; // encoding error: drop rather than log garbage
        if (static_cast<size_t>(n) >= sizeof(buffer)) {
            // Truncation would mislead diagnostics; mark it explicitly.
            constexpr char kTrunc[] = "...<truncated>";
            std::memcpy(buffer + sizeof(buffer) - sizeof(kTrunc), kTrunc, sizeof(kTrunc));
        }
        Log(level, std::string(buffer));
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static constexpr size_t kMaxPendingMessages = 256;

    std::ofstream            m_logFile;
    std::mutex               m_mutex;
    bool                     m_initialized{ false };
    bool                     m_disabled{ false };
    LogLevel                 m_minLevel{ LogLevel::Info };
    std::vector<std::string> m_pending; // messages logged before Initialize()/Disable()
    size_t                   m_droppedPending{ 0 }; // buffered messages dropped beyond cap
};

} // namespace cbr

#define CBR_LOG_DEBUG(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define CBR_LOG_INFO(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Info, fmt, ##__VA_ARGS__)
#define CBR_LOG_WARN(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define CBR_LOG_ERROR(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Error, fmt, ##__VA_ARGS__)
