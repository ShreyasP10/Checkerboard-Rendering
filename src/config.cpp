#include "cbr/config.h"
#include "cbr/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <charconv>

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

} // namespace

ConfigManager& ConfigManager::Get() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::filesystem::path& configPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_WARN("Configuration file not found at %s. Using default settings.", configPath.string().c_str());
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        auto eqPos = trimmed.find('=');
        if (eqPos != std::string::npos) {
            std::string key = Trim(trimmed.substr(0, eqPos));
            std::string val = Trim(StripComment(trimmed.substr(eqPos + 1)));

            if (key == "Enabled") {
                m_config.enabled = ParseBool(val, m_config.enabled);
            } else if (key == "TargetWidth") {
                m_config.targetWidth = ParseUInt(val, m_config.targetWidth, 720, 7680) & ~1u; // Ensure even width
            } else if (key == "TargetHeight") {
                m_config.targetHeight = ParseUInt(val, m_config.targetHeight, 480, 4320) & ~1u; // Ensure even height
            } else if (key == "PreferredApi") {
                std::string apiUpper = ToUpper(val);
                if (apiUpper == "VULKAN") m_config.preferredApi = GraphicsApi::Vulkan;
                else if (apiUpper == "D3D12") m_config.preferredApi = GraphicsApi::D3D12;
                else if (apiUpper == "AUTO")  m_config.preferredApi = GraphicsApi::Auto;
                else CBR_LOG_WARN("Unknown PreferredApi '%s' (expected Vulkan, D3D12 or Auto); keeping default.", val.c_str());
            } else if (key == "MipLodBias") {
                m_config.mipLodBias = ParseFloat(val, m_config.mipLodBias, -4.0f, 4.0f);
            } else if (key == "DepthTolerance") {
                m_config.depthTolerance = ParseFloat(val, m_config.depthTolerance, 0.0001f, 1.0f);
            } else if (key == "EnableColorClamping") {
                m_config.enableColorClamping = ParseBool(val, m_config.enableColorClamping);
            } else if (key == "ColorSpace") {
                m_config.colorSpace = (ToUpper(val) == "RGB") ? ColorSpace::RGB : ColorSpace::YCoCg;
            } else if (key == "HistoryWeight") {
                m_config.historyWeight = ParseFloat(val, m_config.historyWeight, 0.0f, 1.0f);
            } else if (key == "EnableSpatialFallback") {
                m_config.enableSpatialFallback = ParseBool(val, m_config.enableSpatialFallback);
            } else if (key == "EnableMotionDilation") {
                m_config.enableMotionDilation = ParseBool(val, m_config.enableMotionDilation);
            } else if (key == "DepthConvention") {
                std::string dc = ToUpper(val);
                if (dc == "STANDARD") m_config.depthConvention = DepthConvention::Standard;
                else if (dc == "REVERSED") m_config.depthConvention = DepthConvention::Reversed;
                else CBR_LOG_WARN("Unknown DepthConvention '%s' (expected Standard or Reversed); keeping previous.", val.c_str());
            } else if (key == "DepthNear") {
                m_config.depthNear = ParseFloat(val, m_config.depthNear, 0.001f, 100.0f);
            } else if (key == "DepthFar") {
                m_config.depthFar = ParseFloat(val, m_config.depthFar, 0.0f, 1000000.0f);
            } else if (key == "JitterPattern") {
                if (ToUpper(val) == "HALTON") {
                    CBR_LOG_WARN("JitterPattern=Halton is not implemented yet; using Checkerboard.");
                }
                m_config.jitterPattern = JitterPattern::Checkerboard;
            } else if (key == "JitterScale") {
                m_config.jitterScale = ParseFloat(val, m_config.jitterScale, 0.1f, 4.0f);
                if (m_config.jitterScale != 1.0f) {
                    CBR_LOG_WARN("JitterScale != 1.0 is ignored for 2x MSAA checkerboard geometry; coverage requires exactly one pixel.");
                }
            } else if (key == "JitterDirection") {
                try {
                    int d = std::stoi(val);
                    if (d == 1 || d == -1) {
                        m_config.jitterDirection = d;
                    } else {
                        CBR_LOG_WARN("Invalid JitterDirection '%s' (expected +1 or -1); keeping previous.", val.c_str());
                    }
                } catch (...) {
                    CBR_LOG_WARN("Invalid JitterDirection '%s'; keeping previous.", val.c_str());
                }
            } else if (key == "JitterCompensation") {
                m_config.jitterCompensation = ParseFloat(val, m_config.jitterCompensation, -1.0f, 1.0f);
            } else if (key == "DebugView") {
                m_config.debugView = ParseUInt(val, m_config.debugView, 0, 4);
            } else if (key == "ShowOverlay") {
                m_config.showOverlay = ParseBool(val, m_config.showOverlay);
            } else if (key == "LogToFile") {
                m_config.logToFile = ParseBool(val, m_config.logToFile);
            } else if (key == "LogLevel") {
                m_config.logLevel = val;
            }
        }
    }

    CBR_LOG_INFO("Configuration successfully loaded from %s (Target: %ux%u, API: %s, CBR Enabled: %s)",
        configPath.string().c_str(),
        m_config.targetWidth,
        m_config.targetHeight,
        m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan"
            : m_config.preferredApi == GraphicsApi::D3D12 ? "D3D12" : "Auto",
        m_config.enabled ? "true" : "false");

    return true;
}

bool ConfigManager::Save(const std::filesystem::path& configPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_ERROR("Failed to open %s for saving configuration.", configPath.string().c_str());
        return false;
    }

    file << "; RDR2 Checkerboard Rendering Mod Configuration\n";
    file << "[General]\n";
    file << "Enabled = " << (m_config.enabled ? "true" : "false") << "\n";
    file << "TargetWidth = " << m_config.targetWidth << "\n";
    file << "TargetHeight = " << m_config.targetHeight << "\n";
    file << "PreferredApi = "
         << (m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan"
           : m_config.preferredApi == GraphicsApi::D3D12 ? "D3D12" : "Auto") << "\n";
    file << "MipLodBias = " << m_config.mipLodBias << "\n\n";

    file << "[Reconstruction]\n";
    file << "DepthTolerance = " << m_config.depthTolerance << "\n";
    file << "EnableColorClamping = " << (m_config.enableColorClamping ? "true" : "false") << "\n";
    file << "ColorSpace = " << (m_config.colorSpace == ColorSpace::RGB ? "RGB" : "YCoCg") << "\n";
    file << "HistoryWeight = " << m_config.historyWeight << "\n";
    file << "EnableSpatialFallback = " << (m_config.enableSpatialFallback ? "true" : "false") << "\n";
    file << "EnableMotionDilation = " << (m_config.enableMotionDilation ? "true" : "false") << "\n";
    file << "DepthConvention = " << (m_config.depthConvention == DepthConvention::Standard ? "Standard" : "Reversed") << "\n";
    file << "DepthNear = " << m_config.depthNear << "\n";
    file << "DepthFar = " << m_config.depthFar << "\n\n";

    file << "[Jitter]\n";
    file << "JitterPattern = " << (m_config.jitterPattern == JitterPattern::Halton ? "Halton" : "Checkerboard") << "\n";
    file << "JitterScale = " << m_config.jitterScale << "\n";
    file << "JitterDirection = " << m_config.jitterDirection << "\n";
    file << "JitterCompensation = " << m_config.jitterCompensation << "\n\n";

    file << "[Debug]\n";
    file << "ShowOverlay = " << (m_config.showOverlay ? "true" : "false") << "\n";
    file << "DebugView = " << m_config.debugView << "\n";
    file << "LogToFile = " << (m_config.logToFile ? "true" : "false") << "\n";
    file << "LogLevel = " << m_config.logLevel << "\n";

    return true;
}

} // namespace cbr
