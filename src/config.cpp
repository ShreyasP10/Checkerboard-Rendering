#include "cbr/config.h"
#include "cbr/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace cbr {

namespace {

std::string Trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool ParseBool(const std::string& val, bool defaultVal) {
    std::string s = val;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "0" || s == "no" || s == "off") return false;
    return defaultVal;
}

} // namespace

ConfigManager& ConfigManager::Get() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_WARN("Configuration file not found at %s. Using default settings.", configPath.c_str());
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
            std::string val = Trim(trimmed.substr(eqPos + 1));

            if (key == "Enabled") {
                m_config.enabled = ParseBool(val, m_config.enabled);
            } else if (key == "TargetWidth") {
                m_config.targetWidth = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "TargetHeight") {
                m_config.targetHeight = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "PreferredApi") {
                if (val == "Vulkan") m_config.preferredApi = GraphicsApi::Vulkan;
                else if (val == "D3D12") m_config.preferredApi = GraphicsApi::D3D12;
            } else if (key == "MipLodBias") {
                m_config.mipLodBias = std::stof(val);
            } else if (key == "DepthTolerance") {
                m_config.depthTolerance = std::stof(val);
            } else if (key == "EnableColorClamping") {
                m_config.enableColorClamping = ParseBool(val, m_config.enableColorClamping);
            } else if (key == "ColorSpace") {
                m_config.colorSpace = (val == "RGB") ? ColorSpace::RGB : ColorSpace::YCoCg;
            } else if (key == "HistoryWeight") {
                m_config.historyWeight = std::stof(val);
            } else if (key == "EnableSpatialFallback") {
                m_config.enableSpatialFallback = ParseBool(val, m_config.enableSpatialFallback);
            } else if (key == "DebugView") {
                m_config.debugView = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "ShowOverlay") {
                m_config.showOverlay = ParseBool(val, m_config.showOverlay);
            } else if (key == "LogToFile") {
                m_config.logToFile = ParseBool(val, m_config.logToFile);
            }
        }
    }

    CBR_LOG_INFO("Configuration successfully loaded from %s (Target: %ux%u, API: %s, CBR Enabled: %s)",
        configPath.c_str(),
        m_config.targetWidth,
        m_config.targetHeight,
        m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan" : "D3D12",
        m_config.enabled ? "true" : "false");

    return true;
}

bool ConfigManager::Save(const std::string& configPath) {
    std::ofstream file(configPath);
    if (!file.is_open()) {
        CBR_LOG_ERROR("Failed to open %s for saving configuration.", configPath.c_str());
        return false;
    }

    file << "; RDR2 Checkerboard Rendering Mod Configuration\n";
    file << "[General]\n";
    file << "Enabled = " << (m_config.enabled ? "true" : "false") << "\n";
    file << "TargetWidth = " << m_config.targetWidth << "\n";
    file << "TargetHeight = " << m_config.targetHeight << "\n";
    file << "PreferredApi = " << (m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan" : "D3D12") << "\n";
    file << "MipLodBias = " << m_config.mipLodBias << "\n\n";

    file << "[Reconstruction]\n";
    file << "DepthTolerance = " << m_config.depthTolerance << "\n";
    file << "EnableColorClamping = " << (m_config.enableColorClamping ? "true" : "false") << "\n";
    file << "ColorSpace = " << (m_config.colorSpace == ColorSpace::RGB ? "RGB" : "YCoCg") << "\n";
    file << "HistoryWeight = " << m_config.historyWeight << "\n";
    file << "EnableSpatialFallback = " << (m_config.enableSpatialFallback ? "true" : "false") << "\n\n";

    file << "[Debug]\n";
    file << "ShowOverlay = " << (m_config.showOverlay ? "true" : "false") << "\n";
    file << "DebugView = " << m_config.debugView << "\n";
    file << "LogToFile = " << (m_config.logToFile ? "true" : "false") << "\n";

    return true;
}

} // namespace cbr
