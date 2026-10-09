#include "cbr/logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace cbr {

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::filesystem::path& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return;
    }

    m_disabled = false;
    m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    m_initialized = m_logFile.is_open();

    if (m_initialized) {
        m_logFile << "=================================================================\n";
        m_logFile << " RDR2 Checkerboard Rendering Mod (CBR) Log Initialized           \n";
        m_logFile << " Maintainer: Shreyas Pawar                                       \n";
        m_logFile << " Target: NVIDIA GeForce GTX 1070 Ti & Vulkan / DX12              \n";
        m_logFile << "=================================================================\n";
        for (const auto& line : m_pending) {
            m_logFile << line;
        }
        if (m_droppedPending > 0) {
            m_logFile << "[WARN] " << m_droppedPending << " early log message(s) dropped (buffer cap "
                      << kMaxPendingMessages << ").\n";
        }
        m_logFile.flush();
    }
    m_pending.clear();
    m_droppedPending = 0;
}

void Logger::Disable() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_disabled = true;
    m_pending.clear();
    m_droppedPending = 0;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized && m_logFile.is_open()) {
        m_logFile << "[INFO] Logger shutting down.\n";
        m_logFile.flush();
        m_logFile.close();
    }
    m_initialized = false;
    m_disabled = true;
    m_pending.clear();
    m_droppedPending = 0;
}

void Logger::SetMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (level < m_minLevel) {
        return;
    }

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO";  break;
        case LogLevel::Warning: levelStr = "WARN";  break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &in_time_t);
#else
    localtime_r(&in_time_t, &timeInfo);
#endif

    std::stringstream ss;
    ss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count()
       << " [" << levelStr << "] " << message << "\n";

    std::string formatted = ss.str();

    if (m_initialized && m_logFile.is_open()) {
        m_logFile << formatted;
        m_logFile.flush();
    } else if (!m_initialized && !m_disabled) {
        if (m_pending.size() < kMaxPendingMessages) {
            m_pending.push_back(formatted);
        } else {
            ++m_droppedPending;
        }
    }

#if defined(_DEBUG)
    std::cout << formatted;
#endif
}

} // namespace cbr
