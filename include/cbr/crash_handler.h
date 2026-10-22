#pragma once

#include <filesystem>
#include <string>

namespace cbr {

class CrashHandler {
public:
    static CrashHandler& Get();

    // Installs top-level unhandled exception filter and crash telemetry interceptor
    void Install(const std::filesystem::path& logDirectory);
    void Uninstall();

    bool IsInstalled() const { return m_installed; }

    // Logs detailed system and game runtime telemetry to cbr.log
    void LogSystemTelemetry();

private:
    CrashHandler() = default;
    ~CrashHandler() = default;
    CrashHandler(const CrashHandler&) = delete;
    CrashHandler& operator=(const CrashHandler&) = delete;

    bool m_installed{ false };
    std::filesystem::path m_logDirectory;
};

} // namespace cbr
