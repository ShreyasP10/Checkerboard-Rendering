#include "cbr/config.h"
#include "cbr/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <cctype>
#include <system_error>
#include <vector>

namespace cbr {

namespace {

std::string Trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Strip inline comments starting with ';' or '#'
std::string StripComment(const std::string& str) {
    auto pos = str.find_first_of(";#");
    if (pos != std::string::npos) {
        return str.substr(0, pos);
    }
    return str;
}

bool ParseBool(const std::string& val, bool defaultVal) {
    std::string s = val;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return defaultVal;
}

uint32_t ParseUInt(const std::string& val, uint32_t defaultVal, uint32_t minVal, uint32_t maxVal) {
    if (val.empty()) return defaultVal;
    try {
        // std::stoul accepts a leading '-' (wrapping around) and ignores trailing text ("4k" -> 4);
        // reject both so typos fall back to the default instead of becoming a bogus value.
        if (val.front() == '-') return defaultVal;
        size_t idx = 0;
        unsigned long result = std::stoul(val, &idx);
        if (idx == 0 || idx != val.size()) return defaultVal;
        if (result < minVal) result = minVal;
        if (result > maxVal) result = maxVal;
        return static_cast<uint32_t>(result);
    } catch (...) {
        return defaultVal;
    }
}

float ParseFloat(const std::string& val, float defaultVal, float minVal, float maxVal) {
    if (val.empty()) return defaultVal;
    try {
        size_t idx = 0;
        float result = std::stof(val, &idx);
        if (idx == 0 || idx != val.size() || std::isnan(result) || std::isinf(result)) return defaultVal;
        if (result < minVal) result = minVal;
        if (result > maxVal) result = maxVal;
        return result;
    } catch (...) {
        return defaultVal;
    }
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

uint32_t MakeEvenClamped(uint32_t v) { return MakeEvenUp(v); }

bool TryParseIntStrict(const std::string& val, int& out) {
    if (val.empty()) return false;
    try {
        size_t pos = 0;
        int v = std::stoi(val, &pos);
        if (pos != val.size()) return false; // reject trailing garbage ("1xyz")
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string SanitizeLogLevel(const std::string& val, const std::string& fallback) {
    std::string v = Trim(val);
    if (v.size() > 16) v.resize(16);
    // Strip CR/LF to prevent log injection via Save().
    v.erase(std::remove(v.begin(), v.end(), '\n'), v.end());
    v.erase(std::remove(v.begin(), v.end(), '\r'), v.end());
    std::string u = ToUpper(v);
    if (u == "DEBUG" || u == "INFO" || u == "WARN" || u == "WARNING" || u == "ERROR") return v;
    return fallback;
}

} // namespace

ConfigManager& ConfigManager::Get() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::filesystem::path& configPath) {
    // Stat first: reject absurd files before reading (DoS guard). Do not hold
    // m_mutex during I/O or logging (lock-order: config -> logger would deadlock
    // if a log callback ever touched config; Modify() callbacks have same rule).
    {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(configPath, ec);
        if (!ec && sz > kMaxConfigFileBytes) {
            CBR_LOG_ERROR("Config file %s too large (%llu bytes, cap %zu); using defaults.",
                configPath.filename().string().c_str(),
                static_cast<unsigned long long>(sz), kMaxConfigFileBytes);
            return false;
        }
    }
    std::ifstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_WARN("Configuration file %s not found. Using default settings.",
            configPath.filename().string().c_str());
        return false;
    }

    CBRConfig parsed = GetConfig(); // start from current settings
    std::vector<std::string> warnings;
    std::string line;
    std::string currentSection;
    size_t lineCount = 0;

    while (std::getline(file, line)) {
        if (++lineCount > kMaxConfigLines) {
            warnings.emplace_back("cbr.ini truncated: too many lines");
            break;
        }
        if (line.size() > kMaxConfigLineChars) {
            warnings.emplace_back("oversized line ignored");
            continue;
        }
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = ToUpper(Trim(trimmed.substr(1, trimmed.size() - 2)));
            continue;
        }

        auto eqPos = trimmed.find('=');
        if (eqPos != std::string::npos) {
            std::string key = Trim(trimmed.substr(0, eqPos));
            std::string val = Trim(StripComment(trimmed.substr(eqPos + 1)));

            if (key == "Enabled") {
                parsed.enabled = ParseBool(val, parsed.enabled);
            } else if (key == "TargetWidth") {
                parsed.targetWidth = MakeEvenUp(ParseUInt(val, parsed.targetWidth, kMinTargetWidth, kMaxTargetWidth));
            } else if (key == "TargetHeight") {
                parsed.targetHeight = MakeEvenUp(ParseUInt(val, parsed.targetHeight, kMinTargetHeight, kMaxTargetHeight));
            } else if (key == "PreferredApi") {
                std::string apiUpper = ToUpper(val);
                if (apiUpper == "VULKAN") parsed.preferredApi = GraphicsApi::Vulkan;
                else if (apiUpper == "D3D12") parsed.preferredApi = GraphicsApi::D3D12;
                else if (apiUpper == "AUTO")  parsed.preferredApi = GraphicsApi::Auto;
                else warnings.emplace_back("Unknown PreferredApi '" + val + "'; keeping previous.");
            } else if (key == "MipLodBias") {
                parsed.mipLodBias = ParseFloat(val, parsed.mipLodBias, -4.0f, 4.0f);
            } else if (key == "DepthTolerance") {
                parsed.depthTolerance = ParseFloat(val, parsed.depthTolerance, 0.0001f, 1.0f);
            } else if (key == "EnableColorClamping") {
                parsed.enableColorClamping = ParseBool(val, parsed.enableColorClamping);
            } else if (key == "ColorSpace") {
                parsed.colorSpace = (ToUpper(val) == "RGB") ? ColorSpace::RGB : ColorSpace::YCoCg;
            } else if (key == "HistoryWeight") {
                parsed.historyWeight = ParseFloat(val, parsed.historyWeight, 0.0f, 1.0f);
            } else if (key == "EnableSpatialFallback") {
                parsed.enableSpatialFallback = ParseBool(val, parsed.enableSpatialFallback);
            } else if (key == "EnableMotionDilation") {
                parsed.enableMotionDilation = ParseBool(val, parsed.enableMotionDilation);
            } else if (key == "DepthConvention") {
                std::string dc = ToUpper(val);
                if (dc == "STANDARD") parsed.depthConvention = DepthConvention::Standard;
                else if (dc == "REVERSED") parsed.depthConvention = DepthConvention::Reversed;
                else warnings.emplace_back("Unknown DepthConvention '" + val + "'; keeping previous.");
            } else if (key == "DepthNear") {
                parsed.depthNear = ParseFloat(val, parsed.depthNear, 0.001f, 100.0f);
            } else if (key == "DepthFar") {
                parsed.depthFar = ParseFloat(val, parsed.depthFar, 0.0f, 1000000.0f);
            } else if (key == "JitterPattern") {
                if (ToUpper(val) == "HALTON") {
                    warnings.emplace_back("JitterPattern=Halton is not implemented yet; using Checkerboard.");
                }
                parsed.jitterPattern = JitterPattern::Checkerboard;
            } else if (key == "JitterScale") {
                parsed.jitterScale = ParseFloat(val, parsed.jitterScale, 0.1f, 4.0f);
                if (parsed.jitterScale != 1.0f) {
                    warnings.emplace_back("JitterScale != 1.0 is ignored for 2x MSAA checkerboard geometry.");
                }
            } else if (key == "JitterDirection") {
                int d = 0;
                if (TryParseIntStrict(val, d) && (d == 1 || d == -1)) {
                    parsed.jitterDirection = d;
                } else {
                    warnings.emplace_back("Invalid JitterDirection '" + val + "'; keeping previous.");
                }
            } else if (key == "ProjectionJitterSign") {
                int s = 0;
                if (TryParseIntStrict(val, s) && (s == 1 || s == -1)) {
                    parsed.projectionJitterSign = s;
                } else {
                    warnings.emplace_back("Invalid ProjectionJitterSign '" + val + "'; keeping previous.");
                }
            } else if (key == "JitterCompensation") {
                parsed.jitterCompensation = ParseFloat(val, parsed.jitterCompensation, -1.0f, 1.0f);
            } else if (key == "DebugView") {
                parsed.debugView = ParseUInt(val, parsed.debugView, 0, 5);
            } else if (key == "ShowOverlay") {
                parsed.showOverlay = ParseBool(val, parsed.showOverlay);
            } else if (key == "LogToFile") {
                parsed.logToFile = ParseBool(val, parsed.logToFile);
            } else if (key == "LogLevel") {
                const std::string clean = SanitizeLogLevel(val, parsed.logLevel);
                if (clean != Trim(val)) warnings.emplace_back("Invalid LogLevel sanitized.");
                parsed.logLevel = clean;
            } else {
                warnings.emplace_back("Unknown key '" + key + "' in [" + currentSection + "]; ignored.");
            }
        }
    }

    UpdateConfig(parsed);
    for (const auto& w : warnings) CBR_LOG_WARN("%s", w.c_str());

    const CBRConfig applied = GetConfig();
    CBR_LOG_INFO("Configuration loaded from %s (Target: %ux%u, API: %s, CBR Enabled: %s)",
        configPath.filename().string().c_str(),
        applied.targetWidth,
        applied.targetHeight,
        applied.preferredApi == GraphicsApi::Vulkan ? "Vulkan"
            : applied.preferredApi == GraphicsApi::D3D12 ? "D3D12" : "Auto",
        applied.enabled ? "true" : "false");

    return true;
}

bool ConfigManager::Save(const std::filesystem::path& configPath) {
    const CBRConfig snapshot = GetConfig(); // copy under lock; do I/O outside
    std::ofstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_ERROR("Failed to open %s for saving configuration.", configPath.filename().string().c_str());
        return false;
    }

    file << "; RDR2 Checkerboard Rendering Mod Configuration\n";
    file << "[General]\n";
    file << "Enabled = " << (snapshot.enabled ? "true" : "false") << "\n";
    file << "TargetWidth = " << snapshot.targetWidth << "\n";
    file << "TargetHeight = " << snapshot.targetHeight << "\n";
    file << "PreferredApi = "
         << (snapshot.preferredApi == GraphicsApi::Vulkan ? "Vulkan"
           : snapshot.preferredApi == GraphicsApi::D3D12 ? "D3D12" : "Auto") << "\n";
    file << "MipLodBias = " << snapshot.mipLodBias << "\n\n";

    file << "[Reconstruction]\n";
    file << "DepthTolerance = " << snapshot.depthTolerance << "\n";
    file << "EnableColorClamping = " << (snapshot.enableColorClamping ? "true" : "false") << "\n";
    file << "ColorSpace = " << (snapshot.colorSpace == ColorSpace::RGB ? "RGB" : "YCoCg") << "\n";
    file << "HistoryWeight = " << snapshot.historyWeight << "\n";
    file << "EnableSpatialFallback = " << (snapshot.enableSpatialFallback ? "true" : "false") << "\n";
    file << "EnableMotionDilation = " << (snapshot.enableMotionDilation ? "true" : "false") << "\n";
    file << "DepthConvention = " << (snapshot.depthConvention == DepthConvention::Standard ? "Standard" : "Reversed") << "\n";
    file << "DepthNear = " << snapshot.depthNear << "\n";
    file << "DepthFar = " << snapshot.depthFar << "\n\n";

    file << "[Jitter]\n";
    // Halton is reserved/not implemented: always persist Checkerboard so a
    // round-trip never claims Halton support.
    file << "JitterPattern = Checkerboard ; Halton reserved, not implemented\n";
    file << "JitterScale = " << snapshot.jitterScale << "\n";
    file << "JitterDirection = " << snapshot.jitterDirection << "\n";
    file << "ProjectionJitterSign = " << snapshot.projectionJitterSign << "\n";
    file << "JitterCompensation = " << snapshot.jitterCompensation << "\n\n";

    file << "[Debug]\n";
    file << "ShowOverlay = " << (snapshot.showOverlay ? "true" : "false") << "\n";
    file << "DebugView = " << snapshot.debugView << "\n";
    file << "LogToFile = " << (snapshot.logToFile ? "true" : "false") << "\n";
    file << "LogLevel = " << SanitizeLogLevel(snapshot.logLevel, "Info") << "\n";

    file.flush();
    if (!file.good()) {
        CBR_LOG_ERROR("Failed to write %s (disk full or I/O error); config may be truncated.",
            configPath.filename().string().c_str());
        return false;
    }
    return true;
}

} // namespace cbr
