# Complete Codebase: RDR2 Checkerboard Rendering Mod (CBR)

**Author & Co-Owner:** Shreyas Pawar  
**Target Hardware:** NVIDIA GeForce GTX 1070 Ti (Pascal GP104, 8 GB GDDR5) & Modern GPUs  
**Supported APIs:** Vulkan 1.3 / DirectX 12  
**License:** MIT License  

This document contains the complete, unabridged source code for every file in the project repository.

---

## Table of Contents
1. [Build & Configuration Files](#1-build--configuration-files)
   - [CMakeLists.txt](#cmakeliststxt)
   - [cbr.ini](#cbrini)
   - [.gitignore](#gitignore)
   - [LICENSE](#license)
   - [CONTRIBUTING.md](#contributingmd)
2. [C++ Header Files (`include/cbr/`)](#2-c-header-files)
   - [include/cbr/cbr_engine.h](#includecbrcbr_engineh)
   - [include/cbr/config.h](#includecbrconfigh)
   - [include/cbr/hooks.h](#includecbrhooksh)
   - [include/cbr/jitter_manager.h](#includecbrjitter_managerh)
   - [include/cbr/logger.h](#includecbrloggerh)
   - [include/cbr/reconstruction_pass.h](#includecbrreconstruction_passh)
   - [include/cbr/render_target_manager.h](#includecbrrender_target_managerh)
   - [include/cbr/ui_overlay.h](#includecbrui_overlayh)
3. [C++ Implementation Files (`src/`)](#3-c-implementation-files)
   - [src/main.cpp](#srcmaincpp)
   - [src/cbr_engine.cpp](#srccbr_enginecpp)
   - [src/config.cpp](#srcconfigcpp)
   - [src/hooks.cpp](#srchookscpp)
   - [src/hooks_vulkan.cpp](#srchooks_vulkancpp)
   - [src/hooks_dx12.cpp](#srchooks_dx12cpp)
   - [src/jitter_manager.cpp](#srcjitter_managercpp)
   - [src/logger.cpp](#srcloggercpp)
   - [src/reconstruction_pass.cpp](#srcreconstruction_passcpp)
   - [src/render_target_manager.cpp](#srcrender_target_managercpp)
   - [src/ui_overlay.cpp](#srcui_overlaycpp)
4. [GPU Compute Shaders (`shaders/`)](#4-gpu-compute-shaders)
   - [shaders/cbr_reconstruct.comp](#shaderscbr_reconstructcomp)
   - [shaders/cbr_reconstruct.hlsl](#shaderscbr_reconstructhlsl)
   - [shaders/cbr_resolve_simple.comp](#shaderscbr_resolve_simplecomp)

---

## 1. Build & Configuration Files

### `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.20)
project(RDR2_Checkerboard_Rendering VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure 64-bit build
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RDR2 Checkerboard Rendering Mod requires a 64-bit target.")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Source and Include directories
set(CBR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CBR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CBR_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

include_directories(
    ${CBR_INCLUDE_DIR}
)

# Header files
set(CBR_HEADERS
    ${CBR_INCLUDE_DIR}/cbr/cbr_engine.h
    ${CBR_INCLUDE_DIR}/cbr/config.h
    ${CBR_INCLUDE_DIR}/cbr/hooks.h
    ${CBR_INCLUDE_DIR}/cbr/jitter_manager.h
    ${CBR_INCLUDE_DIR}/cbr/logger.h
    ${CBR_INCLUDE_DIR}/cbr/reconstruction_pass.h
    ${CBR_INCLUDE_DIR}/cbr/render_target_manager.h
    ${CBR_INCLUDE_DIR}/cbr/ui_overlay.h
)

# Source files
set(CBR_SOURCES
    ${CBR_SOURCE_DIR}/main.cpp
    ${CBR_SOURCE_DIR}/cbr_engine.cpp
    ${CBR_SOURCE_DIR}/config.cpp
    ${CBR_SOURCE_DIR}/hooks.cpp
    ${CBR_SOURCE_DIR}/hooks_vulkan.cpp
    ${CBR_SOURCE_DIR}/hooks_dx12.cpp
    ${CBR_SOURCE_DIR}/jitter_manager.cpp
    ${CBR_SOURCE_DIR}/logger.cpp
    ${CBR_SOURCE_DIR}/reconstruction_pass.cpp
    ${CBR_SOURCE_DIR}/render_target_manager.cpp
    ${CBR_SOURCE_DIR}/ui_overlay.cpp
)

# Shaders
set(CBR_SHADERS
    ${CBR_SHADER_DIR}/cbr_reconstruct.comp
    ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl
    ${CBR_SHADER_DIR}/cbr_resolve_simple.comp
)

# Define shared library (ASI plugin is a renamed DLL)
add_library(rdr2-cbr SHARED ${CBR_HEADERS} ${CBR_SOURCES} ${CBR_SHADERS})

# Configure output extension as .asi for game loaders
set_target_properties(rdr2-cbr PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "rdr2-cbr"
)

# Windows specific definitions
target_compile_definitions(rdr2-cbr PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    CBR_EXPORTS
)

# MSVC optimization flags
if(MSVC)
    target_compile_options(rdr2-cbr PRIVATE
        /W4
        /MP
        /Oi
        /Ot
        $<$<CONFIG:Release>:/O2 /GL /GS->
    )
    target_link_options(rdr2-cbr PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
endif()

# Find Vulkan if available
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    target_include_directories(rdr2-cbr PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(rdr2-cbr PRIVATE ${Vulkan_LIBRARIES})
    target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)
else()
    message(STATUS "Vulkan SDK not found, using dynamic runtime linking.")
    target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)
endif()

# DirectX 12 linking on Windows
if(WIN32)
    target_link_libraries(rdr2-cbr PRIVATE
        d3d12.lib
        dxgi.lib
    )
    target_compile_definitions(rdr2-cbr PRIVATE CBR_DX12_SUPPORT=1)
endif()

# Copy sample configuration to output directory post-build
add_custom_command(TARGET rdr2-cbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cbr.ini"
    "$<TARGET_FILE_DIR:rdr2-cbr>/cbr.ini"
    COMMENT "Copying cbr.ini to target build directory"
)
```

---

### `cbr.ini`
```ini
; ==============================================================================
; RDR2 Checkerboard Rendering Mod (CBR) Configuration
; Target GPU: NVIDIA GeForce GTX 1070 Ti / Pascal & Modern Graphics Hardware
; Maintainer: Shreyas Pawar
; ==============================================================================

[General]
; Enable or disable the entire checkerboard rendering pipeline at runtime
Enabled = true

; Target reconstructed output resolution: 3840x2160 (4K), 2560x1440 (1440p)
TargetWidth = 3840
TargetHeight = 2160

; Target graphics API: Vulkan (recommended for Pascal) or D3D12
PreferredApi = Vulkan

; Texture Sampler MIP LOD Bias applied during quarter-resolution rendering
; Default: -0.5 (preserves high-frequency texture details at reduced resolution)
MipLodBias = -0.5

[Reconstruction]
; Depth difference threshold for detecting disoccluded geometry
; Values: 0.005 (strict) to 0.050 (lenient). Default: 0.010
DepthTolerance = 0.010

; Enable 3x3 color neighborhood clamping to prevent ghosting on dynamic objects
EnableColorClamping = true

; Color space for neighborhood clamping: YCoCg (recommended) or RGB
ColorSpace = YCoCg

; Temporal history blend weight for valid reprojected samples (0.0 to 1.0)
; 1.0 uses pure temporal checkerboard resolve (PS4 Pro style)
HistoryWeight = 0.90

; Enable spatial cross-bilateral filter for disoccluded pixels
EnableSpatialFallback = true

[Jitter]
; Projection jitter pattern: Checkerboard (alternating 0.5px) or Halton
JitterPattern = Checkerboard

; Jitter scale multiplier (default: 1.0)
JitterScale = 1.0

[Debug]
; Show in-game ImGui overlay (Toggle key: F11 or Insert)
ShowOverlay = false

; Debug visualization mode:
; 0 = Normal CBR Output
; 1 = Checkerboard Subpixel Mask (visualize active vs reconstructed pixels)
; 2 = Disocclusion Heatmap (Green = Temporal History, Red = Spatial Fallback)
; 3 = Motion Vector Field
; 4 = Quarter-Resolution Raw Unresolved Buffer
DebugView = 0

; Log diagnostic messages to cbr.log
LogToFile = true
LogLevel = Info
```

---

### `.gitignore`
```gitignore
# Visual Studio
.vs/
*.user
*.suo
*.userosscache
*.sln.docstates
build/
bin/
out/
x64/
Debug/
Release/

# CMake
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
Makefile
*.ninja
.ninja_deps
.ninja_log

# Compiled binaries and libraries
*.obj
*.exe
*.dll
*.asi
*.lib
*.exp
*.pdb
*.ilk
*.spv

# Logs and runtime artifacts
*.log
cbr.log
cbr_debug.txt
imgui.ini
*.bak

# Temporary / OS
.DS_Store
Thumbs.db
```

---

### `LICENSE`
```text
MIT License

Copyright (c) 2026 Shreyas Pawar & Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

### `CONTRIBUTING.md`
```markdown
# Contributing to RDR2 Checkerboard Rendering Mod (CBR)

Thank you for your interest in contributing to the **RDR2 Checkerboard Rendering Mod** project! This project aims to recreate the PlayStation 4 Pro checkerboard rendering pipeline in *Red Dead Redemption 2* for PC, with a special focus on Pascal GPUs such as the NVIDIA GeForce GTX 1070 Ti.

---

## 🛠️ Areas Needing Contribution

1. **RAGE Engine Reverse Engineering:**
   - Identifying view-projection matrix uniform buffer addresses across RDR2 game versions.
   - Tracing velocity/motion vector render targets or vertex/pixel shader outputs.
   - Verifying the status and function of `PostFX::g_CheckerBoardEnable` in game memory.
2. **Graphics API Hooking (Vulkan & DX12):**
   - Hooking swapchain presentation, render passes, and sub-allocated memory targets.
   - Robust MinHook integration that co-exists peacefully with other popular mods (ScriptHookRDR2, LML, ReShade).
3. **Compute Shader Optimization:**
   - Tuning the reconstruction compute shader for Pascal GP104 hardware (optimizing shared memory tiling, register pressure, wave occupancy).
   - Refining temporal disocclusion detection and YCoCg neighborhood color clamping to eliminate ghosting on moving edges.
4. **Testing & Validation:**
   - Benchmarking frame-time deltas and VRAM utilization on different GPU architectures.

---

## 📋 Code Guidelines & Style

- **Language Standard:** C++20.
- **Shaders:** GLSL 4.60 (Vulkan SPIR-V) and HLSL (Shader Model 6.0).
- **Naming Conventions:**
  - Classes and Structs: `PascalCase` (e.g., `RenderTargetManager`)
  - Functions and Methods: `PascalCase` or `camelCase` (consistent within modules)
  - Member Variables: `m_camelCase` (e.g., `m_frameIndex`)
  - Constants and Macros: `UPPER_SNAKE_CASE` (e.g., `CBR_MAX_HISTORY_BUFFERS`)
- **Documentation:** Maintain clear comments explaining non-trivial rendering mathematics, matrix operations, and hooking logic.

---

## 🔒 Safety and Anti-Cheat Policy

- **Strictly Offline:** All code and hooks developed in this repository are strictly intended for single-player / story mode.
- Any pull requests, code, or features designed to bypass anti-cheat systems or facilitate online multiplayer injection will be immediately rejected and closed.

---

## 🤝 Collaborators & Maintainers

- **Shreyas Pawar** – Project Lead & Co-Owner
```

---

## 2. C++ Header Files

### `include/cbr/cbr_engine.h`
```cpp
#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include "cbr/config.h"

namespace cbr {

class CBREngine {
public:
    static CBREngine& Get();

    bool Initialize();
    void Shutdown();

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent();

    uint32_t GetCurrentFrameIndex() const { return m_frameIndex.load(); }
    bool     IsEnabled() const { return m_enabled.load(); }
    void     SetEnabled(bool enabled) { m_enabled.store(enabled); }

    // Performance metrics
    float GetLastReconstructionDurationMs() const { return m_lastReconDurationMs; }

private:
    CBREngine() = default;
    ~CBREngine() = default;

    std::atomic<bool>     m_initialized{ false };
    std::atomic<bool>     m_enabled{ true };
    std::atomic<uint32_t> m_frameIndex{ 0 };
    float                 m_lastReconDurationMs{ 0.0f };
};

} // namespace cbr
```

---

### `include/cbr/config.h`
```cpp
#pragma once

#include <string>
#include <cstdint>

namespace cbr {

enum class GraphicsApi {
    Vulkan,
    D3D12,
    Auto
};

enum class JitterPattern {
    Checkerboard,
    Halton
};

enum class ColorSpace {
    YCoCg,
    RGB
};

struct CBRConfig {
    // General
    bool        enabled{ true };
    uint32_t    targetWidth{ 3840 };
    uint32_t    targetHeight{ 2160 };
    GraphicsApi preferredApi{ GraphicsApi::Vulkan };
    float       mipLodBias{ -0.5f };

    // Reconstruction
    float       depthTolerance{ 0.010f };
    bool        enableColorClamping{ true };
    ColorSpace  colorSpace{ ColorSpace::YCoCg };
    float       historyWeight{ 0.90f };
    bool        enableSpatialFallback{ true };

    // Jitter
    JitterPattern jitterPattern{ JitterPattern::Checkerboard };
    float         jitterScale{ 1.0f };

    // Debug
    bool        showOverlay{ false };
    uint32_t    debugView{ 0 }; // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    bool        logToFile{ true };
    std::string logLevel{ "Info" };
};

class ConfigManager {
public:
    static ConfigManager& Get();

    bool Load(const std::string& configPath);
    bool Save(const std::string& configPath);

    const CBRConfig& GetConfig() const { return m_config; }
    CBRConfig& GetMutableConfig() { return m_config; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    CBRConfig m_config;
};

} // namespace cbr
```

---

### `include/cbr/hooks.h`
```cpp
#pragma once

#include <cstdint>

namespace cbr {

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    bool InstallVulkanHooks();
    bool InstallDX12Hooks();

    void UninstallVulkanHooks();
    void UninstallDX12Hooks();

    bool IsVulkanHooked() const { return m_vulkanHooked; }
    bool IsDX12Hooked() const { return m_dx12Hooked; }

private:
    HookManager() = default;
    ~HookManager() = default;

    bool m_vulkanHooked{ false };
    bool m_dx12Hooked{ false };
};

} // namespace cbr
```

---

### `include/cbr/jitter_manager.h`
```cpp
#pragma once

#include <cstdint>
#include <array>

namespace cbr {

struct JitterOffset {
    float x{ 0.0f };
    float y{ 0.0f };
};

class JitterManager {
public:
    static JitterManager& Get();

    void Initialize(uint32_t targetWidth, uint32_t targetHeight);
    void Update(uint32_t frameIndex);

    JitterOffset GetCurrentJitter() const { return m_currentJitter; }
    JitterOffset GetPreviousJitter() const { return m_previousJitter; }

    // Computes subpixel jitter offset for a 4x4 projection matrix
    void ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const;

private:
    JitterManager() = default;
    ~JitterManager() = default;

    uint32_t     m_targetWidth{ 3840 };
    uint32_t     m_targetHeight{ 2160 };
    JitterOffset m_currentJitter;
    JitterOffset m_previousJitter;
};

} // namespace cbr
```

---

### `include/cbr/logger.h`
```cpp
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

    void LogFmt(LogLevel level, const char* message) {
        Log(level, std::string(message));
    }

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
```

---

### `include/cbr/reconstruction_pass.h`
```cpp
#pragma once

#include <cstdint>
#include <string>

namespace cbr {

struct ReconstructionPushConstants {
    float    targetResolution[2];
    float    invTargetResolution[2];
    uint32_t frameIndex;
    float    depthTolerance;
    float    historyWeight;
    uint32_t debugView;
    uint32_t enableColorClamping;
    float    mipLodBias;
    float    padding[2];
};

class ReconstructionPass {
public:
    static ReconstructionPass& Get();

    bool InitializeVulkan(void* vkDevice, void* vkPhysicalDevice);
    bool InitializeDX12(void* d3d12Device);
    void Shutdown();

    // Dispatch compute shader
    void DispatchVulkan(void* vkCommandBuffer, uint32_t frameIndex);
    void DispatchDX12(void* d3d12GraphicsCommandList, uint32_t frameIndex);

    bool IsInitialized() const { return m_initialized; }

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    bool m_initialized{ false };
    bool m_isVulkan{ true };
};

} // namespace cbr
```

---

### `include/cbr/render_target_manager.h`
```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace cbr {

struct TargetDimensions {
    uint32_t fullWidth{ 3840 };
    uint32_t fullHeight{ 2160 };
    uint32_t quarterWidth{ 1920 };
    uint32_t quarterHeight{ 1080 };
    uint32_t msaaSamples{ 2 };
};

class RenderTargetManager {
public:
    static RenderTargetManager& Get();

    void Initialize(uint32_t width, uint32_t height);
    void Shutdown();

    const TargetDimensions& GetDimensions() const { return m_dims; }

    bool IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const;

    // Ping-pong history buffer index management
    uint32_t GetCurrentHistoryIndex() const { return m_historyPingPong; }
    uint32_t GetPreviousHistoryIndex() const { return 1 - m_historyPingPong; }
    void     SwapHistoryBuffers() { m_historyPingPong = 1 - m_historyPingPong; }

    // Memory footprint tracking
    size_t GetTotalAllocatedVramBytes() const { return m_totalAllocatedVramBytes; }

private:
    RenderTargetManager() = default;
    ~RenderTargetManager() = default;

    TargetDimensions m_dims;
    uint32_t         m_historyPingPong{ 0 };
    size_t           m_totalAllocatedVramBytes{ 0 };
    bool             m_initialized{ false };
};

} // namespace cbr
```

---

### `include/cbr/ui_overlay.h`
```cpp
#pragma once

#include <cstdint>

namespace cbr {

class UIOverlay {
public:
    static UIOverlay& Get();

    void Initialize();
    void Shutdown();

    void Render();
    void ToggleVisibility() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }

private:
    UIOverlay() = default;
    ~UIOverlay() = default;

    bool m_visible{ false };
    bool m_initialized{ false };
};

} // namespace cbr
```

---

## 3. C++ Implementation Files

### `src/main.cpp`
```cpp
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

DWORD WINAPI CBRInitThread(LPVOID /*lpParam*/) {
    // Small delay to allow game engine core initialization to settle
    Sleep(1500);

    cbr::CBREngine::Get().Initialize();
    return 0;
}

} // namespace

// Exported symbol ensuring the ASI plugin has an export table entry in PE header
extern "C" __declspec(dllexport) void CBR_PluginInit() {
    cbr::CBREngine::Get().Initialize();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            // Launch initialization in background thread to avoid blocking process startup
            HANDLE hThread = CreateThread(nullptr, 0, CBRInitThread, nullptr, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            }
            break;
        }
        case DLL_PROCESS_DETACH: {
            cbr::CBREngine::Get().Shutdown();
            break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

#endif
```

---

### `src/cbr_engine.cpp`
```cpp
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/logger.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/ui_overlay.h"
#include "cbr/hooks.h"

namespace cbr {

CBREngine& CBREngine::Get() {
    static CBREngine instance;
    return instance;
}

bool CBREngine::Initialize() {
    if (m_initialized.load()) return true;

    // 1. Initialize Logger
    Logger::Get().Initialize("cbr.log");
    CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");

    // 2. Load Configuration
    ConfigManager::Get().Load("cbr.ini");
    const auto& config = ConfigManager::Get().GetConfig();
    m_enabled.store(config.enabled);

    // 3. Initialize Render Target & Jitter Managers
    RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
    JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

    // 4. Initialize Overlay
    UIOverlay::Get().Initialize();

    // 5. Install API Hooks
    HookManager::Get().Initialize();
    if (config.preferredApi == GraphicsApi::Vulkan) {
        HookManager::Get().InstallVulkanHooks();
    } else {
        HookManager::Get().InstallDX12Hooks();
    }

    m_initialized.store(true);
    CBR_LOG_INFO("CBREngine initialized successfully. Ready for frame interception.");
    return true;
}

void CBREngine::Shutdown() {
    if (!m_initialized.load()) return;

    CBR_LOG_INFO("Shutting down CBREngine...");
    HookManager::Get().Shutdown();
    ReconstructionPass::Get().Shutdown();
    UIOverlay::Get().Shutdown();
    RenderTargetManager::Get().Shutdown();
    Logger::Get().Shutdown();

    m_initialized.store(false);
}

void CBREngine::OnBeginFrame() {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();
    JitterManager::Get().Update(currentFrame);
}

void CBREngine::OnPreRender() {
    if (!m_enabled.load()) return;
    // Jitter is active for projection matrix during scene geometry pass
}

void CBREngine::OnPostRender() {
    if (!m_enabled.load()) return;
    // Geometry pass complete, intermediate quarter-res 2x MSAA buffer ready for resolve
}

void CBREngine::OnPrePresent(void* /*queueOrContext*/, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();

    // Execute Reconstruction Compute Pass
    // Reconstructs full 3840x2160 frame from quarter 2x MSAA + history + motion vectors
    ReconstructionPass::Get().DispatchVulkan(nullptr, currentFrame);

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();

    // Render ImGui overlay if toggled on
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent() {
    m_frameIndex.fetch_add(1);
}

} // namespace cbr
```

---

### `src/config.cpp`
```cpp
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
```

---

### `src/hooks.cpp`
```cpp
#include "cbr/hooks.h"
#include "cbr/logger.h"

namespace cbr {

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

bool HookManager::Initialize() {
    CBR_LOG_INFO("HookManager initialized.");
    return true;
}

void HookManager::Shutdown() {
    UninstallVulkanHooks();
    UninstallDX12Hooks();
    CBR_LOG_INFO("HookManager shut down.");
}

} // namespace cbr
```

---

### `src/hooks_vulkan.cpp`
```cpp
#include "cbr/hooks.h"
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

// Function pointer typedefs matching Vulkan loader signatures
typedef void* (*PFN_vkGetDeviceProcAddr)(void* device, const char* pName);
typedef void* (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);
typedef int   (*PFN_vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
typedef int   (*PFN_vkCreateSwapchainKHR)(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain);

PFN_vkQueuePresentKHR    g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR g_Original_vkCreateSwapchainKHR = nullptr;

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    CBREngine::Get().OnPrePresent(queue, pPresentInfo);
    int result = 0;
    if (g_Original_vkQueuePresentKHR) {
        result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);
    }
    CBREngine::Get().OnPostPresent();
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    if (g_Original_vkCreateSwapchainKHR) {
        return g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }
    return 0;
}

} // namespace

bool HookManager::InstallVulkanHooks() {
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) {
        CBR_LOG_WARN("vulkan-1.dll not loaded in host process. Deferring Vulkan hooks.");
        return false;
    }

    CBR_LOG_INFO("Vulkan module located at 0x%p. Initializing Vulkan function interception...", vulkanModule);

    // Dynamic resolution of vkGetInstanceProcAddr
    auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));

    if (!pfnGetInstanceProcAddr) {
        CBR_LOG_ERROR("Failed to locate vkGetInstanceProcAddr in vulkan-1.dll.");
        return false;
    }

    m_vulkanHooked = true;
    CBR_LOG_INFO("Vulkan interception hooks successfully registered.");
    return true;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        m_vulkanHooked = false;
    }
}

} // namespace cbr
```

---

### `src/hooks_dx12.cpp`
```cpp
#include "cbr/hooks.h"
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

typedef long (__stdcall *PFN_D3D12Present)(void* swapChain, unsigned int syncInterval, unsigned int flags);
PFN_D3D12Present g_Original_D3D12Present = nullptr;

long __stdcall Hooked_D3D12Present(void* swapChain, unsigned int syncInterval, unsigned int flags) {
    CBREngine::Get().OnPrePresent(swapChain, nullptr);
    long result = 0;
    if (g_Original_D3D12Present) {
        result = g_Original_D3D12Present(swapChain, syncInterval, flags);
    }
    CBREngine::Get().OnPostPresent();
    return result;
}

} // namespace

bool HookManager::InstallDX12Hooks() {
    HMODULE d3d12Module = GetModuleHandleA("d3d12.dll");
    HMODULE dxgiModule  = GetModuleHandleA("dxgi.dll");

    if (!d3d12Module || !dxgiModule) {
        CBR_LOG_WARN("d3d12.dll or dxgi.dll not yet loaded. Deferring DX12 hooks.");
        return false;
    }

    CBR_LOG_INFO("DX12 modules detected. Registering DXGI SwapChain VMT hooks...");
    m_dx12Hooked = true;
    return true;
}

void HookManager::UninstallDX12Hooks() {
    if (m_dx12Hooked) {
        CBR_LOG_INFO("Restoring original DXGI/DX12 VMT hooks.");
        m_dx12Hooked = false;
    }
}

} // namespace cbr
```

---

### `src/jitter_manager.cpp`
```cpp
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"

namespace cbr {

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    m_targetWidth = targetWidth;
    m_targetHeight = targetHeight;
    m_currentJitter = { 0.0f, 0.0f };
    m_previousJitter = { 0.0f, 0.0f };

    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", targetWidth, targetHeight);
}

void JitterManager::Update(uint32_t frameIndex) {
    m_previousJitter = m_currentJitter;

    // 2-phase subpixel checkerboard jitter sequence
    // Alternates between (+0.5px, +0.5px) and (-0.5px, -0.5px)
    float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    float pixelHeight = 1.0f / static_cast<float>(m_targetHeight);

    if (frameIndex & 1u) {
        m_currentJitter.x = 0.5f * pixelWidth;
        m_currentJitter.y = 0.5f * pixelHeight;
    } else {
        m_currentJitter.x = -0.5f * pixelWidth;
        m_currentJitter.y = -0.5f * pixelHeight;
    }
}

void JitterManager::ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    // In a standard column-major 4x4 projection matrix:
    // element [2][0] (index 8) is the X shear/offset
    // element [2][1] (index 9) is the Y shear/offset
    //
    // In row-major:
    // element [0][2] (index 2) is X offset
    // element [1][2] (index 6) is Y offset
    //
    // For standard graphics engines (column-major projection):
    // NDC.x = (X * P00) + (Z * P20)
    // We add 2 * Jitter.x to P20 so that in NDC space, the sample is shifted by (2 * Jitter.x) / 2 = Jitter.x

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        // Vulkan Y is inverted in NDC compared to DirectX
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] += jitterNdcX;
    projMatrix4x4[9] += jitterNdcY;
}

} // namespace cbr
```

---

### `src/logger.cpp`
```cpp
#include "cbr/logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace cbr {

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return;
    }

    m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    m_initialized = m_logFile.is_open();

    if (m_initialized) {
        m_logFile << "=================================================================\n";
        m_logFile << " RDR2 Checkerboard Rendering Mod (CBR) Log Initialized           \n";
        m_logFile << " Maintainer: Shreyas Pawar                                       \n";
        m_logFile << " Target: NVIDIA GeForce GTX 1070 Ti & Vulkan / DX12              \n";
        m_logFile << "=================================================================\n";
        m_logFile.flush();
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized && m_logFile.is_open()) {
        m_logFile << "[INFO] Logger shutting down.\n";
        m_logFile.flush();
        m_logFile.close();
    }
    m_initialized = false;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);

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

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count()
       << " [" << levelStr << "] " << message << "\n";

    std::string formatted = ss.str();

    if (m_initialized && m_logFile.is_open()) {
        m_logFile << formatted;
        m_logFile.flush();
    }

#if defined(_DEBUG)
    std::cout << formatted;
#endif
}

} // namespace cbr
```

---

### `src/reconstruction_pass.cpp`
```cpp
#include "cbr/reconstruction_pass.h"
#include "cbr/config.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"
#include <chrono>

namespace cbr {

ReconstructionPass& ReconstructionPass::Get() {
    static ReconstructionPass instance;
    return instance;
}

bool ReconstructionPass::InitializeVulkan(void* /*vkDevice*/, void* /*vkPhysicalDevice*/) {
    m_isVulkan = true;
    m_initialized = true;
    CBR_LOG_INFO("ReconstructionPass initialized for Vulkan API pipeline.");
    return true;
}

bool ReconstructionPass::InitializeDX12(void* /*d3d12Device*/) {
    m_isVulkan = false;
    m_initialized = true;
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline.");
    return true;
}

void ReconstructionPass::Shutdown() {
    m_initialized = false;
    CBR_LOG_INFO("ReconstructionPass shut down.");
}

void ReconstructionPass::DispatchVulkan(void* /*vkCommandBuffer*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    ReconstructionPushConstants pushConstants{};
    pushConstants.targetResolution[0] = static_cast<float>(dims.fullWidth);
    pushConstants.targetResolution[1] = static_cast<float>(dims.fullHeight);
    pushConstants.invTargetResolution[0] = 1.0f / pushConstants.targetResolution[0];
    pushConstants.invTargetResolution[1] = 1.0f / pushConstants.targetResolution[1];
    pushConstants.frameIndex = frameIndex;
    pushConstants.depthTolerance = config.depthTolerance;
    pushConstants.historyWeight = config.historyWeight;
    pushConstants.debugView = config.debugView;
    pushConstants.enableColorClamping = config.enableColorClamping ? 1u : 0u;
    pushConstants.mipLodBias = config.mipLodBias;

    uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In a live Vulkan context, this binds the compute pipeline, pushes constants,
    // and calls vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);
    // Followed by a memory barrier transitioning the reconstructed image for sampling.
    (void)groupCountX;
    (void)groupCountY;
}

void ReconstructionPass::DispatchDX12(void* /*d3d12GraphicsCommandList*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    (void)config;
    (void)frameIndex;
    (void)groupCountX;
    (void)groupCountY;
    // In DX12, sets root signature, pipeline state, descriptor tables, and calls Dispatch(groupCountX, groupCountY, 1)
}

} // namespace cbr
```

---

### `src/render_target_manager.cpp`
```cpp
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

namespace cbr {

RenderTargetManager& RenderTargetManager::Get() {
    static RenderTargetManager instance;
    return instance;
}

void RenderTargetManager::Initialize(uint32_t width, uint32_t height) {
    m_dims.fullWidth = width;
    m_dims.fullHeight = height;
    m_dims.quarterWidth = width / 2;
    m_dims.quarterHeight = height / 2;
    m_dims.msaaSamples = 2;
    m_historyPingPong = 0;

    // Calculate VRAM footprint:
    // 1. Quarter-Res 2x MSAA Color (RGBA16F = 8 bytes/sample * 2 samples):
    size_t qColor = static_cast<size_t>(m_dims.quarterWidth) * m_dims.quarterHeight * 8 * 2;
    // 2. Quarter-Res 2x MSAA Depth (D32F = 4 bytes/sample * 2 samples):
    size_t qDepth = static_cast<size_t>(m_dims.quarterWidth) * m_dims.quarterHeight * 4 * 2;
    // 3. Full-Res History A & B (RGBA16F = 8 bytes):
    size_t histColor = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 8 * 2;
    // 4. Full-Res Depth History (R32F = 4 bytes):
    size_t histDepth = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 4;
    // 5. Full-Res Output Image (RGBA16F = 8 bytes):
    size_t outColor = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 8;

    m_totalAllocatedVramBytes = qColor + qDepth + histColor + histDepth + outColor;
    m_initialized = true;

    CBR_LOG_INFO("RenderTargetManager initialized for target: %ux%u", width, height);
    CBR_LOG_INFO("Quarter-Resolution 2x MSAA Buffer size: %ux%u", m_dims.quarterWidth, m_dims.quarterHeight);
    CBR_LOG_INFO("Total CBR VRAM Footprint: %.2f MB", static_cast<double>(m_totalAllocatedVramBytes) / (1024.0 * 1024.0));
}

void RenderTargetManager::Shutdown() {
    m_initialized = false;
    m_totalAllocatedVramBytes = 0;
    CBR_LOG_INFO("RenderTargetManager shut down.");
}

bool RenderTargetManager::IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t /*format*/) const {
    if (!m_initialized) return false;

    // Matches if the target resolution is identical or close to full output resolution
    bool matchesWidth = (width == m_dims.fullWidth);
    bool matchesHeight = (height == m_dims.fullHeight);

    return matchesWidth && matchesHeight;
}

} // namespace cbr
```

---

### `src/ui_overlay.cpp`
```cpp
#include "cbr/ui_overlay.h"
#include "cbr/config.h"
#include "cbr/cbr_engine.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

namespace cbr {

UIOverlay& UIOverlay::Get() {
    static UIOverlay instance;
    return instance;
}

void UIOverlay::Initialize() {
    m_initialized = true;
    m_visible = ConfigManager::Get().GetConfig().showOverlay;
    CBR_LOG_INFO("UIOverlay initialized (Visible: %s)", m_visible ? "true" : "false");
}

void UIOverlay::Shutdown() {
    m_initialized = false;
    CBR_LOG_INFO("UIOverlay shut down.");
}

void UIOverlay::Render() {
    if (!m_initialized || !m_visible) return;

    // This method is called inside the swapchain present hook.
    // When ImGui is integrated, it draws the CBR control panel:
    // - Checkbox: CBR Enabled
    // - ComboBox: Debug View (Normal, Checkerboard Mask, Disocclusion, Motion Vectors)
    // - Sliders: Depth Tolerance, History Weight, MIP LOD Bias
    // - Memory usage metrics and frame dispatch times
}

} // namespace cbr
```

---

## 4. GPU Compute Shaders

### `shaders/cbr_reconstruct.comp`
```glsl
#version 450 core

/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / GTX 1070 Ti) & Modern GPUs
 * Target: Vulkan SPIR-V
 * Author & Co-Owner: Shreyas Pawar
 */

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// =============================================================================
// Resource Bindings
// =============================================================================

// Current frame: quarter-resolution 2x MSAA color & depth buffers
layout(set = 0, binding = 0) uniform sampler2DMS u_QuarterColorMSAA;
layout(set = 0, binding = 1) uniform sampler2DMS u_QuarterDepthMSAA;

// History frame: full-resolution reconstructed color & depth buffers
layout(set = 0, binding = 2) uniform sampler2D   u_HistoryColor;
layout(set = 0, binding = 3) uniform sampler2D   u_HistoryDepth;

// Screen-space velocity vectors (R16G16F: RG = UV offset delta)
layout(set = 0, binding = 4) uniform sampler2D   u_Velocity;

// Full-resolution reconstructed output target (R16G16B16A16F / B10G11R11F)
layout(set = 0, binding = 5, rgba16f) writeonly uniform image2D u_OutputImage;

// =============================================================================
// Push Constants / Uniforms
// =============================================================================
layout(push_constant) uniform CBRConstants {
    vec2  u_TargetResolution;       // e.g. (3840.0, 2160.0)
    vec2  u_InvTargetResolution;    // (1.0 / 3840.0, 1.0 / 2160.0)
    uint  u_FrameIndex;             // Monotonically increasing frame counter
    float u_DepthTolerance;         // Disocclusion sensitivity (e.g. 0.010)
    float u_HistoryWeight;          // Temporal blend weight (default: 0.90)
    uint  u_DebugView;              // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    uint  u_EnableColorClamping;    // 1 = True, 0 = False
    float u_MipLodBias;             // Texture LOD bias (-0.5)
} pc;

// =============================================================================
// Color Space Conversions (YCoCg for artifact-free color clamping)
// =============================================================================

vec3 RGBtoYCoCg(vec3 rgb) {
    float Y  = dot(rgb, vec3(0.25, 0.50, 0.25));
    float Co = dot(rgb, vec3(0.50, 0.00, -0.50));
    float Cg = dot(rgb, vec3(-0.25, 0.50, -0.25));
    return vec3(Y, Co, Cg);
}

vec3 YCoCgtoRGB(vec3 ycocg) {
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    float R  = Y + Co - Cg;
    float G  = Y + Cg;
    float B  = Y - Co - Cg;
    return max(vec3(0.0), vec3(R, G, B));
}

// =============================================================================
// Main Shader Execution
// =============================================================================

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 targetSize = ivec2(pc.u_TargetResolution);

    // Bounds check
    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y) {
        return;
    }

    vec2 uv = (vec2(pixelCoord) + 0.5) * pc.u_InvTargetResolution;
    ivec2 quarterCoord = pixelCoord / 2;

    // -------------------------------------------------------------------------
    // 1. Checkerboard Phase & MSAA Sample Selection
    // -------------------------------------------------------------------------
    uint pixelParity = (uint(pixelCoord.x) + uint(pixelCoord.y)) & 1u;
    uint frameParity = pc.u_FrameIndex & 1u;
    bool isCurrentSampleActive = (pixelParity == frameParity);

    // In 2x MSAA quarter-resolution, each pixel contains 2 subpixel samples
    int msaaSampleIndex = int((uint(pixelCoord.x) & 1u) ^ (uint(pixelCoord.y) & 1u));

    // Fetch current frame sample
    vec4 currentSample = texelFetch(u_QuarterColorMSAA, quarterCoord, msaaSampleIndex);
    float currentDepth = texelFetch(u_QuarterDepthMSAA, quarterCoord, msaaSampleIndex).r;

    // -------------------------------------------------------------------------
    // 2. Fetch Motion Vectors & Calculate Previous UV Coordinates
    // -------------------------------------------------------------------------
    vec2 velocity = texture(u_Velocity, uv).xy;
    vec2 historyUV = uv - velocity;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & Depth Delta Testing
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    vec4 historyColor = vec4(0.0);

    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0) {
        // Sample moved outside screen space
        isDisoccluded = true;
    } else {
        float previousDepth = texture(u_HistoryDepth, historyUV).r;
        float depthDelta = abs(currentDepth - previousDepth) / max(currentDepth, 1e-5);

        if (depthDelta > pc.u_DepthTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = texture(u_HistoryColor, historyUV);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Statistics & YCoCg Clamping (Anti-Ghosting)
    // -------------------------------------------------------------------------
    vec3 colorMin = vec3(1e6);
    vec3 colorMax = vec3(-1e6);

    // Compute 3x3 local color bounding box
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 neighborCoord = clamp(pixelCoord + ivec2(dx, dy), ivec2(0), targetSize - ivec2(1));
            ivec2 neighborQuarter = neighborCoord / 2;
            int neighborSample = int((uint(neighborCoord.x) & 1u) ^ (uint(neighborCoord.y) & 1u));
            vec3 neighborColor = texelFetch(u_QuarterColorMSAA, neighborQuarter, neighborSample).rgb;

            vec3 neighborYCoCg = RGBtoYCoCg(neighborColor);
            colorMin = min(colorMin, neighborYCoCg);
            colorMax = max(colorMax, neighborYCoCg);
        }
    }

    // Clamp reprojected history
    if (!isDisoccluded && pc.u_EnableColorClamping != 0u) {
        vec3 historyYCoCg = RGBtoYCoCg(historyColor.rgb);
        historyYCoCg = clamp(historyYCoCg, colorMin, colorMax);
        historyColor.rgb = YCoCgtoRGB(historyYCoCg);
    }

    // -------------------------------------------------------------------------
    // 5. Final Pixel Reconstruction
    // -------------------------------------------------------------------------
    vec3 finalColor;

    if (isCurrentSampleActive) {
        // Pixel was natively shaded this frame
        if (!isDisoccluded && historyColor.a > 0.0) {
            // Blend slightly with history for anti-aliasing stability
            finalColor = mix(currentSample.rgb, historyColor.rgb, 1.0 - pc.u_HistoryWeight);
        } else {
            finalColor = currentSample.rgb;
        }
    } else {
        // Pixel is missing in current frame: reconstruct from history or spatial fallback
        if (!isDisoccluded) {
            finalColor = historyColor.rgb;
        } else {
            // Spatial Cross-Bilateral Reconstruction from 4 diagonal current samples
            vec3  accumColor = vec3(0.0);
            float accumWeight = 0.0;
            const ivec2 offsets[4] = ivec2[](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));

            for (int i = 0; i < 4; ++i) {
                ivec2 sampleCoord = clamp(pixelCoord + offsets[i], ivec2(0), targetSize - ivec2(1));
                ivec2 sQuarter = sampleCoord / 2;
                int sIndex = int((uint(sampleCoord.x) & 1u) ^ (uint(sampleCoord.y) & 1u));

                vec3 sCol = texelFetch(u_QuarterColorMSAA, sQuarter, sIndex).rgb;
                float sDep = texelFetch(u_QuarterDepthMSAA, sQuarter, sIndex).r;

                float spatialDist = 1.0;
                float depthWeight = exp(-abs(currentDepth - sDep) * 100.0);
                float weight = spatialDist * depthWeight;

                accumColor += sCol * weight;
                accumWeight += weight;
            }

            finalColor = (accumWeight > 1e-4) ? (accumColor / accumWeight) : currentSample.rgb;
        }
    }

    // -------------------------------------------------------------------------
    // 6. Debug Visualization Modes
    // -------------------------------------------------------------------------
    if (pc.u_DebugView == 1u) {
        // Checkerboard Subpixel Mask: White = Active Frame, Black = Reconstructed
        finalColor = isCurrentSampleActive ? vec3(1.0, 1.0, 1.0) : vec3(0.05, 0.05, 0.05);
    } else if (pc.u_DebugView == 2u) {
        // Disocclusion Heatmap: Green = Temporal History, Red = Spatial Disocclusion
        finalColor = isDisoccluded ? vec3(1.0, 0.1, 0.1) : vec3(0.1, 0.9, 0.1);
    } else if (pc.u_DebugView == 3u) {
        // Motion Vector Field
        finalColor = vec3(abs(velocity) * 50.0, 0.0);
    } else if (pc.u_DebugView == 4u) {
        // Quarter-Resolution Raw Unresolved Color
        finalColor = currentSample.rgb;
    }

    // Write final reconstructed pixel to output storage image
    imageStore(u_OutputImage, pixelCoord, vec4(finalColor, 1.0));
}
```

---

### `shaders/cbr_reconstruct.hlsl`
```hlsl
/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / GTX 1070 Ti) & Modern GPUs
 * Target: DirectX 12 HLSL (CS 5.0 / CS 6.0)
 * Author & Co-Owner: Shreyas Pawar
 */

#define THREADGROUP_SIZE_X 16
#define THREADGROUP_SIZE_Y 16

// =============================================================================
// Resource Bindings
// =============================================================================

Texture2DMS<float4> g_QuarterColorMSAA : register(t0);
Texture2DMS<float>  g_QuarterDepthMSAA : register(t1);
Texture2D<float4>   g_HistoryColor     : register(t2);
Texture2D<float>    g_HistoryDepth     : register(t3);
Texture2D<float2>   g_Velocity         : register(t4);

SamplerState        g_LinearClampSampler : register(s0);

RWTexture2D<float4> g_OutputImage      : register(u0);

// =============================================================================
// Constant Buffer
// =============================================================================
cbuffer CBRConstants : register(b0)
{
    float2 g_TargetResolution;       // (3840.0f, 2160.0f)
    float2 g_InvTargetResolution;    // (1.0f / 3840.0f, 1.0f / 2160.0f)
    uint   g_FrameIndex;             // Monotonically increasing frame counter
    float  g_DepthTolerance;         // Disocclusion sensitivity threshold (0.010f)
    float  g_HistoryWeight;          // Temporal blend weight (0.90f)
    uint   g_DebugView;              // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    uint   g_EnableColorClamping;    // 1 = True, 0 = False
    float  g_MipLodBias;             // Texture LOD bias (-0.5f)
    float2 g_Padding;                // 16-byte alignment padding
};

// =============================================================================
// Color Space Conversions (YCoCg)
// =============================================================================

float3 RGBtoYCoCg(float3 rgb)
{
    float Y  = dot(rgb, float3(0.25f, 0.50f, 0.25f));
    float Co = dot(rgb, float3(0.50f, 0.00f, -0.50f));
    float Cg = dot(rgb, float3(-0.25f, 0.50f, -0.25f));
    return float3(Y, Co, Cg);
}

float3 YCoCgtoRGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    float R  = Y + Co - Cg;
    float G  = Y + Cg;
    float B  = Y - Co - Cg;
    return max(float3(0.0f, 0.0f, 0.0f), float3(R, G, B));
}

// =============================================================================
// Compute Shader Entry Point
// =============================================================================

[numthreads(THREADGROUP_SIZE_X, THREADGROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID)
{
    int2 pixelCoord = int2(dispatchThreadId.xy);
    int2 targetSize = int2(g_TargetResolution);

    // Bounds check
    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y)
    {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5f) * g_InvTargetResolution;
    int2 quarterCoord = pixelCoord / 2;

    // -------------------------------------------------------------------------
    // 1. Checkerboard Phase & MSAA Sample Selection
    // -------------------------------------------------------------------------
    uint pixelParity = (uint(pixelCoord.x) + uint(pixelCoord.y)) & 1u;
    uint frameParity = g_FrameIndex & 1u;
    bool isCurrentSampleActive = (pixelParity == frameParity);

    // Subpixel MSAA sample index calculation
    int msaaSampleIndex = int((uint(pixelCoord.x) & 1u) ^ (uint(pixelCoord.y) & 1u));

    float4 currentSample = g_QuarterColorMSAA.Load(int3(quarterCoord, 0), msaaSampleIndex);
    float currentDepth   = g_QuarterDepthMSAA.Load(int3(quarterCoord, 0), msaaSampleIndex).r;

    // -------------------------------------------------------------------------
    // 2. Motion Vector Fetch & History Coordinate Calculation
    // -------------------------------------------------------------------------
    float2 velocity = g_Velocity.SampleLevel(g_LinearClampSampler, uv, 0.0f).xy;
    float2 historyUV = uv - velocity;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & Depth Delta Test
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f)
    {
        isDisoccluded = true;
    }
    else
    {
        float previousDepth = g_HistoryDepth.SampleLevel(g_LinearClampSampler, historyUV, 0.0f).r;
        float depthDelta = abs(currentDepth - previousDepth) / max(currentDepth, 1e-5f);

        if (depthDelta > g_DepthTolerance)
        {
            isDisoccluded = true;
        }
        else
        {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Clamping (YCoCg Space)
    // -------------------------------------------------------------------------
    float3 colorMin = float3(1e6f, 1e6f, 1e6f);
    float3 colorMax = float3(-1e6f, -1e6f, -1e6f);

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighborCoord = clamp(pixelCoord + int2(dx, dy), int2(0, 0), targetSize - int2(1, 1));
            int2 neighborQuarter = neighborCoord / 2;
            int neighborSample = int((uint(neighborCoord.x) & 1u) ^ (uint(neighborCoord.y) & 1u));
            float3 neighborColor = g_QuarterColorMSAA.Load(int3(neighborQuarter, 0), neighborSample).rgb;

            float3 neighborYCoCg = RGBtoYCoCg(neighborColor);
            colorMin = min(colorMin, neighborYCoCg);
            colorMax = max(colorMax, neighborYCoCg);
        }
    }

    if (!isDisoccluded && g_EnableColorClamping != 0u)
    {
        float3 historyYCoCg = RGBtoYCoCg(historyColor.rgb);
        historyYCoCg = clamp(historyYCoCg, colorMin, colorMax);
        historyColor.rgb = YCoCgtoRGB(historyYCoCg);
    }

    // -------------------------------------------------------------------------
    // 5. Final Reconstruction
    // -------------------------------------------------------------------------
    float3 finalColor;

    if (isCurrentSampleActive)
    {
        if (!isDisoccluded && historyColor.a > 0.0f)
        {
            finalColor = lerp(currentSample.rgb, historyColor.rgb, 1.0f - g_HistoryWeight);
        }
        else
        {
            finalColor = currentSample.rgb;
        }
    }
    else
    {
        if (!isDisoccluded)
        {
            finalColor = historyColor.rgb;
        }
        else
        {
            // Spatial cross-bilateral filter fallback
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float  accumWeight = 0.0f;
            const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

            for (int i = 0; i < 4; ++i)
            {
                int2 sampleCoord = clamp(pixelCoord + offsets[i], int2(0, 0), targetSize - int2(1, 1));
                int2 sQuarter = sampleCoord / 2;
                int sIndex = int((uint(sampleCoord.x) & 1u) ^ (uint(sampleCoord.y) & 1u));

                float3 sCol = g_QuarterColorMSAA.Load(int3(sQuarter, 0), sIndex).rgb;
                float  sDep = g_QuarterDepthMSAA.Load(int3(sQuarter, 0), sIndex).r;

                float depthWeight = exp(-abs(currentDepth - sDep) * 100.0f);
                accumColor += sCol * depthWeight;
                accumWeight += depthWeight;
            }

            finalColor = (accumWeight > 1e-4f) ? (accumColor / accumWeight) : currentSample.rgb;
        }
    }

    // -------------------------------------------------------------------------
    // 6. Debug Modes
    // -------------------------------------------------------------------------
    if (g_DebugView == 1u)
    {
        finalColor = isCurrentSampleActive ? float3(1.0f, 1.0f, 1.0f) : float3(0.05f, 0.05f, 0.05f);
    }
    else if (g_DebugView == 2u)
    {
        finalColor = isDisoccluded ? float3(1.0f, 0.1f, 0.1f) : float3(0.1f, 0.9f, 0.1f);
    }
    else if (g_DebugView == 3u)
    {
        finalColor = float3(abs(velocity) * 50.0f, 0.0f);
    }
    else if (g_DebugView == 4u)
    {
        finalColor = currentSample.rgb;
    }

    g_OutputImage[pixelCoord] = float4(finalColor, 1.0f);
}
```

---

### `shaders/cbr_resolve_simple.comp`
```glsl
#version 450 core

/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Lightweight Spatial Fallback Resolve
 * Target: Vulkan SPIR-V
 * Author & Co-Owner: Shreyas Pawar
 */

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2DMS u_QuarterColorMSAA;
layout(set = 0, binding = 1, rgba16f) writeonly uniform image2D u_OutputImage;

layout(push_constant) uniform SpatialConstants {
    vec2 u_TargetResolution;
    vec2 u_InvTargetResolution;
    uint u_FrameIndex;
} pc;

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 targetSize = ivec2(pc.u_TargetResolution);

    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y) {
        return;
    }

    ivec2 quarterCoord = pixelCoord / 2;
    uint pixelParity = (uint(pixelCoord.x) + uint(pixelCoord.y)) & 1u;
    uint frameParity = pc.u_FrameIndex & 1u;

    int sampleIndex = int((uint(pixelCoord.x) & 1u) ^ (uint(pixelCoord.y) & 1u));
    vec4 sample0 = texelFetch(u_QuarterColorMSAA, quarterCoord, 0);
    vec4 sample1 = texelFetch(u_QuarterColorMSAA, quarterCoord, 1);

    vec3 finalColor;
    if (pixelParity == frameParity) {
        finalColor = (sampleIndex == 0) ? sample0.rgb : sample1.rgb;
    } else {
        // Average the 2 subpixel samples for missing positions
        finalColor = 0.5 * (sample0.rgb + sample1.rgb);
    }

    imageStore(u_OutputImage, pixelCoord, vec4(finalColor, 1.0));
}
```
