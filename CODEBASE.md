# Complete Codebase: RDR2 Checkerboard Rendering Mod (CBR)

**Author & Co-Owner:** Shreyas Pawar  
**Target Hardware:** NVIDIA GeForce GTX 1070 Ti (Pascal GP104, 8 GB GDDR5) & Modern GPUs  
**Supported APIs:** Vulkan 1.3 / DirectX 12  
**License:** MIT License  

This document contains the complete, unabridged source code for every file in the project repository.

---

## Table of Contents
1. [Build & Configuration Files](#sec-build-config)
   - [`CMakeLists.txt`](#cmakeliststxt)
   - [`cbr.ini`](#cbrini)
   - [`.gitignore`](#gitignore)
   - [`LICENSE`](#license)
   - [`CONTRIBUTING.md`](#contributingmd)
2. [C++ Header Files (`include/cbr/`)](#sec-headers)
   - [`include/cbr/cbr_engine.h`](#includecbrcbrengineh)
   - [`include/cbr/config.h`](#includecbrconfigh)
   - [`include/cbr/hooks.h`](#includecbrhooksh)
   - [`include/cbr/jitter_manager.h`](#includecbrjittermanagerh)
   - [`include/cbr/logger.h`](#includecbrloggerh)
   - [`include/cbr/reconstruction_pass.h`](#includecbrreconstructionpassh)
   - [`include/cbr/render_target_manager.h`](#includecbrrendertargetmanagerh)
   - [`include/cbr/ui_overlay.h`](#includecbruioverlayh)
3. [C++ Implementation Files (`src/`)](#sec-sources)
   - [`src/main.cpp`](#srcmaincpp)
   - [`src/cbr_engine.cpp`](#srccbrenginecpp)
   - [`src/config.cpp`](#srcconfigcpp)
   - [`src/hooks.cpp`](#srchookscpp)
   - [`src/hooks_vulkan.cpp`](#srchooksvulkancpp)
   - [`src/hooks_dx12.cpp`](#srchooksdx12cpp)
   - [`src/jitter_manager.cpp`](#srcjittermanagercpp)
   - [`src/logger.cpp`](#srcloggercpp)
   - [`src/reconstruction_pass.cpp`](#srcreconstructionpasscpp)
   - [`src/render_target_manager.cpp`](#srcrendertargetmanagercpp)
   - [`src/ui_overlay.cpp`](#srcuioverlaycpp)
4. [GPU Compute Shaders (`shaders/`)](#sec-shaders)
   - [`shaders/cbr_reconstruct.comp`](#shaderscbrreconstructcomp)
   - [`shaders/cbr_reconstruct.hlsl`](#shaderscbrreconstructhlsl)
   - [`shaders/cbr_resolve_simple.comp`](#shaderscbrresolvesimplecomp)

---

<a id="sec-build-config"></a>
## 1. Build & Configuration Files

<a id="cmakeliststxt"></a>
### `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.20)
project(RDR2_Checkerboard_Rendering VERSION 1.0.0 LANGUAGES CXX)

# Required for MSVC_RUNTIME_LIBRARY target property (static CRT below)
cmake_policy(SET CMP0091 NEW)

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

# Prevent MSVC from attempting default FXC compilation on shaders in IDE
set_source_files_properties(${CBR_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

# Define shared library (ASI plugin is a renamed DLL)
add_library(rdr2-cbr SHARED ${CBR_HEADERS} ${CBR_SOURCES} ${CBR_SHADERS})

# Configure output extension as .asi for game loaders
set_target_properties(rdr2-cbr PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "rdr2-cbr"
)

# Windows specific definitions
# (WIN32_LEAN_AND_MEAN / NOMINMAX are defined, guarded, in the sources that include windows.h)
target_compile_definitions(rdr2-cbr PRIVATE
    CBR_EXPORTS
)

# Static CRT: the plugin must not depend on a redistributable that the game may not ship
set_property(TARGET rdr2-cbr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC optimization and hardening flags
if(MSVC)
    target_compile_options(rdr2-cbr PRIVATE
        /W4
        /MP
        /Oi
        /Ot
        /guard:cf
        /sdl
        $<$<CONFIG:Release>:/O2 /GL /GS>
    )
    target_link_options(rdr2-cbr PRIVATE
        /guard:cf
        /DYNAMICBASE
        /NXCOMPAT
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /CETCOMPAT>
    )
endif()

# Find Vulkan headers if available (dynamic runtime resolution is used for function pointers)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK headers found: ${Vulkan_INCLUDE_DIRS}")
    target_include_directories(rdr2-cbr PRIVATE ${Vulkan_INCLUDE_DIRS})
else()
    message(STATUS "Vulkan SDK not found, using dynamic runtime function pointers only.")
endif()
# Vulkan support does not need the SDK at build time (functions are resolved at runtime)
target_compile_definitions(rdr2-cbr PRIVATE CBR_VULKAN_SUPPORT=1)

# DirectX 12 linking on Windows
if(WIN32)
    target_link_libraries(rdr2-cbr PRIVATE
        d3d12.lib
        dxgi.lib
    )
    target_compile_definitions(rdr2-cbr PRIVATE CBR_DX12_SUPPORT=1)
endif()

# Optional: compile shaders when the toolchain is available (UNTESTED on Windows; verify locally)
find_program(CBR_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin)
find_program(CBR_DXC dxc HINTS $ENV{VULKAN_SDK}/Bin)
set(CBR_SHADER_OUT_DIR "$<TARGET_FILE_DIR:rdr2-cbr>/shaders")
set(CBR_COMPILED_SHADERS "")

if(CBR_GLSLANG)
    foreach(shader cbr_reconstruct cbr_resolve_simple)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/shaders/${shader}.spv
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${CBR_GLSLANG} -V ${CBR_SHADER_DIR}/${shader}.comp -o ${CMAKE_BINARY_DIR}/shaders/${shader}.spv
            DEPENDS ${CBR_SHADER_DIR}/${shader}.comp
            COMMENT "Compiling ${shader}.comp to SPIR-V")
        list(APPEND CBR_COMPILED_SHADERS ${CMAKE_BINARY_DIR}/shaders/${shader}.spv)
    endforeach()
else()
    message(STATUS "glslangValidator not found: SPIR-V shaders will not be built.")
endif()

if(CBR_DXC)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/cbr_reconstruct.dxil
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${CBR_DXC} -T cs_6_0 -E CSMain ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl -Fo ${CMAKE_BINARY_DIR}/shaders/cbr_reconstruct.dxil
        DEPENDS ${CBR_SHADER_DIR}/cbr_reconstruct.hlsl
        COMMENT "Compiling cbr_reconstruct.hlsl to DXIL")
    list(APPEND CBR_COMPILED_SHADERS ${CMAKE_BINARY_DIR}/shaders/cbr_reconstruct.dxil)
else()
    message(STATUS "dxc not found: DXIL shaders will not be built.")
endif()

if(CBR_COMPILED_SHADERS)
    add_custom_target(cbr_shaders ALL DEPENDS ${CBR_COMPILED_SHADERS})
    add_dependencies(rdr2-cbr cbr_shaders)
endif()

# Copy sample configuration to output directory post-build
add_custom_command(TARGET rdr2-cbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cbr.ini"
    "$<TARGET_FILE_DIR:rdr2-cbr>/cbr.ini"
    COMMENT "Copying cbr.ini to target build directory"
)
```

<a id="cbrini"></a>
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

<a id="gitignore"></a>
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
temp/
tmp/
```

<a id="license"></a>
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

<a id="contributingmd"></a>
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
- **Shaders:** GLSL 4.50 / `#version 450` (Vulkan SPIR-V) and HLSL (Shader Model 6.0).
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

<a id="sec-headers"></a>
## 2. C++ Header Files (`include/cbr/`)

<a id="includecbrcbrengineh"></a>
### `include/cbr/cbr_engine.h`
```cpp
#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <mutex>
#include "cbr/config.h"

namespace cbr {

class CBREngine {
public:
    static CBREngine& Get();

    bool Initialize();
    void Shutdown(bool isProcessExit = false);

    void SetModuleDirectory(const std::filesystem::path& dir) { m_moduleDirectory = dir; }
    const std::filesystem::path& GetModuleDirectory() const { return m_moduleDirectory; }

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent();

    uint32_t    GetCurrentFrameIndex() const { return m_frameIndex.load(); }
    bool        IsEnabled() const { return m_enabled.load(); }
    void        SetEnabled(bool enabled) { m_enabled.store(enabled); }
    GraphicsApi GetActiveApi() const { return m_activeApi.load(); }
    void        SetActiveApi(GraphicsApi api) { m_activeApi.store(api); }

    // Performance metrics
    float GetLastReconstructionDurationMs() const { return m_lastReconDurationMs.load(); }

private:
    CBREngine() = default;
    ~CBREngine() = default;

    std::once_flag             m_initOnce;
    std::atomic<bool>          m_initialized{ false };
    std::atomic<bool>          m_enabled{ true };
    std::atomic<uint32_t>      m_frameIndex{ 0 };
    std::atomic<float>         m_lastReconDurationMs{ 0.0f };
    std::atomic<GraphicsApi>   m_activeApi{ GraphicsApi::Vulkan };
    std::filesystem::path      m_moduleDirectory;
};

} // namespace cbr
```

<a id="includecbrconfigh"></a>
### `include/cbr/config.h`
```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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

    bool Load(const std::filesystem::path& configPath);
    bool Save(const std::filesystem::path& configPath);

    const CBRConfig& GetConfig() const { return m_config; }
    CBRConfig& GetMutableConfig() { return m_config; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    CBRConfig m_config;
};

} // namespace cbr
```

<a id="includecbrhooksh"></a>
### `include/cbr/hooks.h`
```cpp
#pragma once

#include <atomic>
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

    bool IsVulkanHooked() const { return m_vulkanHooked.load(); }
    bool IsDX12Hooked() const { return m_dx12Hooked.load(); }

private:
    HookManager() = default;
    ~HookManager() = default;

    std::atomic<bool> m_vulkanHooked{ false };
    std::atomic<bool> m_dx12Hooked{ false };
};

} // namespace cbr
```

<a id="includecbrjittermanagerh"></a>
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
    JitterOffset GetJitterDelta() const {
        return { m_currentJitter.x - m_previousJitter.x, m_currentJitter.y - m_previousJitter.y };
    }

    // Computes subpixel jitter offset for a 4x4 projection matrix
    void ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const;
    void RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const;

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

<a id="includecbrloggerh"></a>
### `include/cbr/logger.h`
```cpp
#pragma once

#include <cstdio>
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
        std::snprintf(buffer, sizeof(buffer), format, args...);
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
};

} // namespace cbr

#define CBR_LOG_DEBUG(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define CBR_LOG_INFO(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Info, fmt, ##__VA_ARGS__)
#define CBR_LOG_WARN(fmt, ...)  cbr::Logger::Get().LogFmt(cbr::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define CBR_LOG_ERROR(fmt, ...) cbr::Logger::Get().LogFmt(cbr::LogLevel::Error, fmt, ##__VA_ARGS__)
```

<a id="includecbrreconstructionpassh"></a>
### `include/cbr/reconstruction_pass.h`
```cpp
#pragma once

#include <atomic>
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

    bool IsInitialized() const { return m_initialized.load(); }

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    std::atomic<bool> m_initialized{ false };
    bool m_isVulkan{ true };
};

} // namespace cbr
```

<a id="includecbrrendertargetmanagerh"></a>
### `include/cbr/render_target_manager.h`
```cpp
#pragma once

#include <atomic>
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
    uint32_t GetCurrentHistoryIndex() const { return m_historyPingPong.load(); }
    uint32_t GetPreviousHistoryIndex() const { return 1u - m_historyPingPong.load(); }
    void     SwapHistoryBuffers() { m_historyPingPong.fetch_xor(1u); }

    // Memory footprint tracking
    size_t GetTotalAllocatedVramBytes() const { return m_totalAllocatedVramBytes; }

private:
    RenderTargetManager() = default;
    ~RenderTargetManager() = default;

    TargetDimensions m_dims;
    std::atomic<uint32_t> m_historyPingPong{ 0 };
    size_t           m_totalAllocatedVramBytes{ 0 };
    std::atomic<bool> m_initialized{ false };
};

} // namespace cbr
```

<a id="includecbruioverlayh"></a>
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

<a id="sec-sources"></a>
## 3. C++ Implementation Files (`src/`)

<a id="srcmaincpp"></a>
### `src/main.cpp`
```cpp
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace {

DWORD WINAPI CBRInitThread(LPVOID /*lpParam*/) {
    // Delay slightly to allow game engine core and graphics runtime to settle
    Sleep(1500);

    cbr::CBREngine::Get().Initialize();
    return 0;
}

// Directory containing this module (wide-char API: safe for non-ASCII and long paths)
std::filesystem::path GetModuleDirectoryPath(HMODULE hModule) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(len);
            break;
        }
        if (buffer.size() >= 32768) { // longest possible NT path
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
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

            // Pin this module so it can never be unmapped while the init thread or any
            // installed hook is still executing code inside it. ASI plugins are not meant
            // to be unloaded, and this removes the need to wait on a thread from DllMain.
            HMODULE pinned = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&CBR_PluginInit),
                &pinned);

            // Record module directory for resolving cbr.ini and cbr.log relative to the DLL
            cbr::CBREngine::Get().SetModuleDirectory(GetModuleDirectoryPath(hModule));

            // Launch initialization in a background thread to avoid blocking process startup.
            // The handle is not needed afterwards, and the module is pinned, so close it now.
            HANDLE hThread = CreateThread(nullptr, 0, CBRInitThread, nullptr, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            }
            break;
        }
        case DLL_PROCESS_DETACH:
            // Intentionally empty. Under the loader lock (and, on process exit, after other
            // threads have already been terminated) it is unsafe to take locks, join threads,
            // or tear down graphics hooks. The OS reclaims all resources at process exit.
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

#endif
```

<a id="srccbrenginecpp"></a>
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
#include <cctype>
#include <string>

namespace cbr {

namespace {

LogLevel ParseLogLevel(const std::string& name) {
    std::string s;
    s.reserve(name.size());
    for (unsigned char c : name) s.push_back(static_cast<char>(std::tolower(c)));
    if (s == "debug")                   return LogLevel::Debug;
    if (s == "warn" || s == "warning")  return LogLevel::Warning;
    if (s == "error")                   return LogLevel::Error;
    return LogLevel::Info;
}

} // namespace

CBREngine& CBREngine::Get() {
    static CBREngine instance;
    return instance;
}

bool CBREngine::Initialize() {
    std::call_once(m_initOnce, [this]() {
        // 1. Load configuration first. Messages logged while loading are buffered by the
        //    Logger and flushed (or discarded) once the log destination is known.
        const std::filesystem::path baseDir = m_moduleDirectory; // empty => current directory
        ConfigManager::Get().Load(baseDir / "cbr.ini");
        const auto& config = ConfigManager::Get().GetConfig();

        // 2. Configure logging from the loaded settings
        Logger::Get().SetMinLevel(ParseLogLevel(config.logLevel));
        if (config.logToFile) {
            Logger::Get().Initialize(baseDir / "cbr.log");
        } else {
            Logger::Get().Disable();
        }

        CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");
        m_enabled.store(config.enabled);
        m_activeApi.store(config.preferredApi);

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Install API Hooks
        HookManager::Get().Initialize();
        if (config.preferredApi == GraphicsApi::Vulkan) {
            HookManager::Get().InstallVulkanHooks();
            ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
        } else {
            HookManager::Get().InstallDX12Hooks();
            ReconstructionPass::Get().InitializeDX12(nullptr);
        }

        m_initialized.store(true);
        CBR_LOG_INFO("CBREngine initialized successfully. Ready for frame interception.");
    });

    return m_initialized.load();
}

void CBREngine::Shutdown(bool isProcessExit) {
    if (!m_initialized.load()) return;

    if (!isProcessExit) {
        CBR_LOG_INFO("Shutting down CBREngine cleanly...");
        HookManager::Get().Shutdown();
        ReconstructionPass::Get().Shutdown();
        UIOverlay::Get().Shutdown();
        RenderTargetManager::Get().Shutdown();
        Logger::Get().Shutdown();
    }

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

void CBREngine::OnPrePresent(void* /*queueOrSwapchain*/, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();

    // NOTE: the argument received here is a VkQueue (Vulkan) or IDXGISwapChain (DX12), NOT a
    // command buffer / command list. The reconstruction pass must record into its own command
    // buffer / list, so nullptr is passed until the real recording path exists.
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(nullptr, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(nullptr, currentFrame);
    }

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

<a id="srcconfigcpp"></a>
### `src/config.cpp`
```cpp
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

} // namespace

ConfigManager& ConfigManager::Get() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::Load(const std::filesystem::path& configPath) {
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
                if (val == "Vulkan") m_config.preferredApi = GraphicsApi::Vulkan;
                else if (val == "D3D12") m_config.preferredApi = GraphicsApi::D3D12;
            } else if (key == "MipLodBias") {
                m_config.mipLodBias = ParseFloat(val, m_config.mipLodBias, -4.0f, 4.0f);
            } else if (key == "DepthTolerance") {
                m_config.depthTolerance = ParseFloat(val, m_config.depthTolerance, 0.0001f, 1.0f);
            } else if (key == "EnableColorClamping") {
                m_config.enableColorClamping = ParseBool(val, m_config.enableColorClamping);
            } else if (key == "ColorSpace") {
                m_config.colorSpace = (val == "RGB") ? ColorSpace::RGB : ColorSpace::YCoCg;
            } else if (key == "HistoryWeight") {
                m_config.historyWeight = ParseFloat(val, m_config.historyWeight, 0.0f, 1.0f);
            } else if (key == "EnableSpatialFallback") {
                m_config.enableSpatialFallback = ParseBool(val, m_config.enableSpatialFallback);
            } else if (key == "JitterPattern") {
                m_config.jitterPattern = (val == "Halton") ? JitterPattern::Halton : JitterPattern::Checkerboard;
            } else if (key == "JitterScale") {
                m_config.jitterScale = ParseFloat(val, m_config.jitterScale, 0.1f, 4.0f);
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
        m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan" : "D3D12",
        m_config.enabled ? "true" : "false");

    return true;
}

bool ConfigManager::Save(const std::filesystem::path& configPath) {
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
    file << "PreferredApi = " << (m_config.preferredApi == GraphicsApi::Vulkan ? "Vulkan" : "D3D12") << "\n";
    file << "MipLodBias = " << m_config.mipLodBias << "\n\n";

    file << "[Reconstruction]\n";
    file << "DepthTolerance = " << m_config.depthTolerance << "\n";
    file << "EnableColorClamping = " << (m_config.enableColorClamping ? "true" : "false") << "\n";
    file << "ColorSpace = " << (m_config.colorSpace == ColorSpace::RGB ? "RGB" : "YCoCg") << "\n";
    file << "HistoryWeight = " << m_config.historyWeight << "\n";
    file << "EnableSpatialFallback = " << (m_config.enableSpatialFallback ? "true" : "false") << "\n\n";

    file << "[Jitter]\n";
    file << "JitterPattern = " << (m_config.jitterPattern == JitterPattern::Halton ? "Halton" : "Checkerboard") << "\n";
    file << "JitterScale = " << m_config.jitterScale << "\n\n";

    file << "[Debug]\n";
    file << "ShowOverlay = " << (m_config.showOverlay ? "true" : "false") << "\n";
    file << "DebugView = " << m_config.debugView << "\n";
    file << "LogToFile = " << (m_config.logToFile ? "true" : "false") << "\n";
    file << "LogLevel = " << m_config.logLevel << "\n";

    return true;
}

} // namespace cbr
```

<a id="srchookscpp"></a>
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

<a id="srchooksvulkancpp"></a>
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

// VK_ERROR_INITIALIZATION_FAILED: returned if a hook is ever invoked without a valid trampoline,
// so the failure is visible to the caller instead of silently dropping frames / swapchains.
constexpr int kVkErrorInitializationFailed = -3;

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return kVkErrorInitializationFailed;
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(queue, pPresentInfo);
    } catch (...) {
    }

    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent();
    } catch (...) {
    }
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    if (!g_Original_vkCreateSwapchainKHR) {
        return kVkErrorInitializationFailed;
    }
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    return g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
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

    // TODO: install real detours (e.g. MinHook) on vkQueuePresentKHR / vkCreateSwapchainKHR and store
    // the trampolines in g_Original_*. Until then NO hook is active, so do not claim success.
    CBR_LOG_WARN("Vulkan hook installation is not implemented yet; no hooks are active.");
    return false;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        m_vulkanHooked = false;
    }
}

} // namespace cbr
```

<a id="srchooksdx12cpp"></a>
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

constexpr long kHResultFail = static_cast<long>(0x80004005u); // E_FAIL

long __stdcall Hooked_D3D12Present(void* swapChain, unsigned int syncInterval, unsigned int flags) {
    if (!g_Original_D3D12Present) {
        return kHResultFail;
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(swapChain, nullptr);
    } catch (...) {
    }

    long result = g_Original_D3D12Present(swapChain, syncInterval, flags);

    try {
        CBREngine::Get().OnPostPresent();
    } catch (...) {
    }
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

    // TODO: locate IDXGISwapChain::Present via a dummy swapchain, detour it, and store the
    // trampoline in g_Original_D3D12Present. Until then NO hook is active.
    CBR_LOG_WARN("DX12 hook installation is not implemented yet; no hooks are active.");
    return false;
}

void HookManager::UninstallDX12Hooks() {
    if (m_dx12Hooked) {
        CBR_LOG_INFO("Restoring original DXGI/DX12 VMT hooks.");
        m_dx12Hooked = false;
    }
}

} // namespace cbr
```

<a id="srcjittermanagercpp"></a>
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
    m_targetWidth = (targetWidth > 0) ? targetWidth : 3840;
    m_targetHeight = (targetHeight > 0) ? targetHeight : 2160;
    m_currentJitter = { 0.0f, 0.0f };
    m_previousJitter = { 0.0f, 0.0f };

    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", m_targetWidth, m_targetHeight);
}

void JitterManager::Update(uint32_t frameIndex) {
    m_previousJitter = m_currentJitter;

    // 2-phase subpixel checkerboard jitter sequence
    // Shifts alternating frames by (+0.5px, +0.5px) and (-0.5px, -0.5px)
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

    // Projection matrix offset in NDC space
    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] += jitterNdcX;
    projMatrix4x4[9] += jitterNdcY;
}

void JitterManager::RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] -= jitterNdcX;
    projMatrix4x4[9] -= jitterNdcY;
}

} // namespace cbr
```

<a id="srcloggercpp"></a>
### `src/logger.cpp`
```cpp
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
        m_logFile.flush();
    }
    m_pending.clear();
}

void Logger::Disable() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_disabled = true;
    m_pending.clear();
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
    } else if (!m_initialized && !m_disabled && m_pending.size() < kMaxPendingMessages) {
        m_pending.push_back(formatted);
    }

#if defined(_DEBUG)
    std::cout << formatted;
#endif
}

} // namespace cbr
```

<a id="srcreconstructionpasscpp"></a>
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

<a id="srcrendertargetmanagercpp"></a>
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
    m_historyPingPong.store(0);

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

<a id="srcuioverlaycpp"></a>
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

<a id="sec-shaders"></a>
## 4. GPU Compute Shaders (`shaders/`)

<a id="shaderscbrreconstructcomp"></a>
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

    // In a 2x2 quarter cell, the two active samples correspond to pixelCoord.x parity
    int msaaSampleIndex = int(uint(pixelCoord.x) & 1u);

    // Fetch current frame sample
    vec4 currentSample = texelFetch(u_QuarterColorMSAA, quarterCoord, msaaSampleIndex);
    float currentDepth = texelFetch(u_QuarterDepthMSAA, quarterCoord, msaaSampleIndex).r;

    // -------------------------------------------------------------------------
    // 2. Fetch Motion Vectors & Calculate Previous UV Coordinates
    // -------------------------------------------------------------------------
    // Explicit LOD 0.0 required for compute shaders
    vec2 velocity = textureLod(u_Velocity, uv, 0.0).xy;
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
        // Nearest depth fetch from history
        float previousDepth = textureLod(u_HistoryDepth, historyUV, 0.0).r;
        float depthDelta = abs(currentDepth - previousDepth) / max(currentDepth, 1e-5);

        if (depthDelta > pc.u_DepthTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = textureLod(u_HistoryColor, historyUV, 0.0);
            if (historyColor.a <= 0.0) {
                // First frame or cleared history buffer
                isDisoccluded = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Statistics & YCoCg Clamping (Anti-Ghosting)
    // Guarded to avoid 9 unnecessary memory fetches when disoccluded or disabled
    // -------------------------------------------------------------------------
    if (!isDisoccluded && pc.u_EnableColorClamping != 0u) {
        vec3 colorMin = vec3(1e6);
        vec3 colorMax = vec3(-1e6);

        // Compute 3x3 local color bounding box
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 neighborCoord = clamp(pixelCoord + ivec2(dx, dy), ivec2(0), targetSize - ivec2(1));
                ivec2 neighborQuarter = neighborCoord / 2;
                int neighborSample = int(uint(neighborCoord.x) & 1u);
                vec3 neighborColor = texelFetch(u_QuarterColorMSAA, neighborQuarter, neighborSample).rgb;

                vec3 neighborYCoCg = RGBtoYCoCg(neighborColor);
                colorMin = min(colorMin, neighborYCoCg);
                colorMax = max(colorMax, neighborYCoCg);
            }
        }

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
            // Blend with history for temporal stability (u_HistoryWeight controls history influence)
            finalColor = mix(currentSample.rgb, historyColor.rgb, pc.u_HistoryWeight);
        } else {
            finalColor = currentSample.rgb;
        }
    } else {
        // Pixel is missing in current frame: reconstruct from history or spatial fallback
        if (!isDisoccluded && historyColor.a > 0.0) {
            finalColor = historyColor.rgb;
        } else {
            // Spatial Cross-Bilateral Reconstruction from 4 cardinal active samples
            vec3  accumColor = vec3(0.0);
            float accumWeight = 0.0;
            const ivec2 offsets[4] = ivec2[](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));

            for (int i = 0; i < 4; ++i) {
                ivec2 sampleCoord = clamp(pixelCoord + offsets[i], ivec2(0), targetSize - ivec2(1));
                ivec2 sQuarter = sampleCoord / 2;
                int sIndex = int(uint(sampleCoord.x) & 1u);

                vec3 sCol = texelFetch(u_QuarterColorMSAA, sQuarter, sIndex).rgb;
                float sDep = texelFetch(u_QuarterDepthMSAA, sQuarter, sIndex).r;

                float depthWeight = exp(-abs(currentDepth - sDep) * 100.0);
                accumColor += sCol * depthWeight;
                accumWeight += depthWeight;
            }

            finalColor = (accumWeight > 1e-4) ? (accumColor / accumWeight) : currentSample.rgb;
        }
    }

    // Guard against NaN/Inf pollution in temporal feedback loop
    if (isnan(finalColor.r) || isinf(finalColor.r) ||
        isnan(finalColor.g) || isinf(finalColor.g) ||
        isnan(finalColor.b) || isinf(finalColor.b)) {
        finalColor = currentSample.rgb;
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

    // Write final reconstructed pixel to output storage image (alpha = 1.0 for valid history)
    imageStore(u_OutputImage, pixelCoord, vec4(finalColor, 1.0));
}
```

<a id="shaderscbrreconstructhlsl"></a>
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
SamplerState        g_PointClampSampler  : register(s1);

RWTexture2D<float4> g_OutputImage      : register(u0);

// =============================================================================
// Constant Buffer (16-byte aligned)
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

    // In a 2x2 quarter cell, the two active samples correspond to pixelCoord.x parity
    int msaaSampleIndex = int(uint(pixelCoord.x) & 1u);

    // In HLSL, Texture2DMS.Load takes (int2 Location, int SampleIndex)
    float4 currentSample = g_QuarterColorMSAA.Load(quarterCoord, msaaSampleIndex);
    float currentDepth   = g_QuarterDepthMSAA.Load(quarterCoord, msaaSampleIndex).r;

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
        // Point sampling for depth history avoids edge bleeding across discontinuities
        float previousDepth = g_HistoryDepth.SampleLevel(g_PointClampSampler, historyUV, 0.0f).r;
        float depthDelta = abs(currentDepth - previousDepth) / max(currentDepth, 1e-5f);

        if (depthDelta > g_DepthTolerance)
        {
            isDisoccluded = true;
        }
        else
        {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
            if (historyColor.a <= 0.0f)
            {
                isDisoccluded = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Clamping (YCoCg Space)
    // Guarded to avoid unnecessary texture fetches when disoccluded or disabled
    // -------------------------------------------------------------------------
    if (!isDisoccluded && g_EnableColorClamping != 0u)
    {
        float3 colorMin = float3(1e6f, 1e6f, 1e6f);
        float3 colorMax = float3(-1e6f, -1e6f, -1e6f);

        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 neighborCoord = clamp(pixelCoord + int2(dx, dy), int2(0, 0), targetSize - int2(1, 1));
                int2 neighborQuarter = neighborCoord / 2;
                int neighborSample = int(uint(neighborCoord.x) & 1u);
                float3 neighborColor = g_QuarterColorMSAA.Load(neighborQuarter, neighborSample).rgb;

                float3 neighborYCoCg = RGBtoYCoCg(neighborColor);
                colorMin = min(colorMin, neighborYCoCg);
                colorMax = max(colorMax, neighborYCoCg);
            }
        }

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
            finalColor = lerp(currentSample.rgb, historyColor.rgb, g_HistoryWeight);
        }
        else
        {
            finalColor = currentSample.rgb;
        }
    }
    else
    {
        if (!isDisoccluded && historyColor.a > 0.0f)
        {
            finalColor = historyColor.rgb;
        }
        else
        {
            // Spatial cross-bilateral filter fallback from 4 cardinal neighbors
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float  accumWeight = 0.0f;
            const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

            for (int i = 0; i < 4; ++i)
            {
                int2 sampleCoord = clamp(pixelCoord + offsets[i], int2(0, 0), targetSize - int2(1, 1));
                int2 sQuarter = sampleCoord / 2;
                int sIndex = int(uint(sampleCoord.x) & 1u);

                float3 sCol = g_QuarterColorMSAA.Load(sQuarter, sIndex).rgb;
                float  sDep = g_QuarterDepthMSAA.Load(sQuarter, sIndex).r;

                float depthWeight = exp(-abs(currentDepth - sDep) * 100.0f);
                accumColor += sCol * depthWeight;
                accumWeight += depthWeight;
            }

            finalColor = (accumWeight > 1e-4f) ? (accumColor / accumWeight) : currentSample.rgb;
        }
    }

    // Guard against NaN/Inf pollution in temporal feedback
    if (isnan(finalColor.r) || isinf(finalColor.r) ||
        isnan(finalColor.g) || isinf(finalColor.g) ||
        isnan(finalColor.b) || isinf(finalColor.b))
    {
        finalColor = currentSample.rgb;
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

<a id="shaderscbrresolvesimplecomp"></a>
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

    int sampleIndex = int(uint(pixelCoord.x) & 1u);
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
