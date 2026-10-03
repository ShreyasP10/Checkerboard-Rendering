# Complete Codebase: RDR2 Checkerboard Rendering Mod (CBR)

**Author & Co-Owner:** Shreyas Pawar  
**Target Hardware:** NVIDIA GeForce GTX 1070 Ti (Pascal GP104) & AMD Radeon Vega / Vega 7 (GCN 5.0)  
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
   - [`include/cbr/checkerboard_mapping.h`](#includecbrcheckerboardmappingh)
   - [`include/cbr/config.h`](#includecbrconfigh)
   - [`include/cbr/depth_convention.h`](#includecbrdepthconventionh)
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
5. [Tests & CI](#sec-tests)
   - [`tests/test_core.cpp`](#teststestcorecpp)
   - [`tests/test_engine.cpp`](#teststestenginecpp)
   - [`.github/workflows/build.yml`](#githubworkflowsbuildyml)

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
    ${CBR_INCLUDE_DIR}/cbr/checkerboard_mapping.h
    ${CBR_INCLUDE_DIR}/cbr/config.h
    ${CBR_INCLUDE_DIR}/cbr/depth_convention.h
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

# Host-side unit tests (portable code only; no Windows APIs or GPU required)
#   cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON
#   cmake --build build-tests --target cbr_tests && ctest --test-dir build-tests --output-on-failure
option(CBR_BUILD_TESTS "Build host-side unit tests" OFF)
if(CBR_BUILD_TESTS)
    enable_testing()
    set(CBR_TEST_CORE_SOURCES
        ${CBR_SOURCE_DIR}/config.cpp
        ${CBR_SOURCE_DIR}/logger.cpp
        ${CBR_SOURCE_DIR}/jitter_manager.cpp
        ${CBR_SOURCE_DIR}/render_target_manager.cpp
        ${CBR_SOURCE_DIR}/reconstruction_pass.cpp
    )
    add_executable(cbr_tests tests/test_core.cpp ${CBR_TEST_CORE_SOURCES})
    target_include_directories(cbr_tests PRIVATE ${CBR_INCLUDE_DIR})
    add_test(NAME cbr_core COMMAND cbr_tests)

    # Engine tests use fake hook installers (the real ones are Windows-only), one mode per process
    add_executable(cbr_engine_tests
        tests/test_engine.cpp
        ${CBR_TEST_CORE_SOURCES}
        ${CBR_SOURCE_DIR}/cbr_engine.cpp
        ${CBR_SOURCE_DIR}/hooks.cpp
        ${CBR_SOURCE_DIR}/ui_overlay.cpp
    )
    target_include_directories(cbr_engine_tests PRIVATE ${CBR_INCLUDE_DIR})
    add_test(NAME cbr_engine      COMMAND cbr_engine_tests)
    add_test(NAME cbr_engine_auto COMMAND cbr_engine_tests auto)
endif()

# Copy sample configuration to output directory post-build
add_custom_command(TARGET rdr2-cbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cbr.ini"
    "$<TARGET_FILE_DIR:rdr2-cbr>/cbr.ini"
    COMMENT "Copying cbr.ini to target build directory"
)

if(CBR_COMPILED_SHADERS)
    add_custom_command(TARGET rdr2-cbr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:rdr2-cbr>/shaders"
        COMMENT "Copying compiled shaders to target build directory"
    )
endif()
```

<a id="cbrini"></a>
### `cbr.ini`
```ini
; ==============================================================================
; RDR2 Checkerboard Rendering Mod (CBR) Configuration
; Target Hardware:
;   - Discrete: NVIDIA GeForce GTX 1070 Ti / Pascal (4K Target: 3840x2160)
;   - APU / Integrated: AMD Radeon Vega 7 / Vega 8 (1080p Target: 1920x1080)
; Maintainer: Shreyas Pawar
; ==============================================================================

[General]
; Enable or disable the entire checkerboard rendering pipeline at runtime
Enabled = true

; Target reconstructed output resolution:
;   - 3840x2160 (4K): Recommended for GTX 1070 Ti (reconstructed from 1080p 2x MSAA)
;   - 1920x1080 (1080p): Recommended for AMD Radeon Vega 7 APUs (reconstructed from 540p 2x MSAA)
TargetWidth = 3840
TargetHeight = 2160

; Target graphics API: Vulkan (recommended for Pascal), D3D12, or Auto (use whichever runtime is loaded)
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

; 3x3 closest-depth motion-vector dilation (cleaner moving silhouettes).
; Costs 9 extra MSAA depth fetches per pixel: set false on bandwidth-limited GPUs.
EnableMotionDilation = true

; Depth buffer convention: Reversed (1 near .. 0 far, RDR2 default) or Standard (0 near .. 1 far)
DepthConvention = Reversed

; Camera near clip distance in metres (used to linearize depth for disocclusion tests)
DepthNear = 0.1

; Camera far clip distance in metres (0.0 = infinite far plane, valid for Reversed only)
DepthFar = 0.0

[Jitter]
; Projection jitter pattern: Checkerboard (alternating whole-pixel shift). Halton is reserved/not implemented yet.
JitterPattern = Checkerboard

; Jitter scale multiplier (default: 1.0)
JitterScale = 1.0

; Odd-frame sampling-grid shift direction in presentation pixels (+1 or -1). Default: 1
JitterDirection = 1

; Sub-pixel jitter compensation applied when reprojecting history.
; Default: 0.0 (the whole-pixel 2x MSAA checkerboard shift is absorbed by the sample mapping,
; so history reprojection needs no jitter delta compensation).
JitterCompensation = 0.0

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

    // One-time setup (config, logging, buffers, overlay). Does NOT depend on the graphics runtime being loaded.
    bool Initialize();
    // Attempts to install the graphics hooks for the active API. Safe to call repeatedly (e.g. from a
    // retry loop while the game loads its graphics runtime); returns true once hooks are installed.
    bool TryInstallHooks();
    void Shutdown(bool isProcessExit = false);

    void SetModuleDirectory(const std::filesystem::path& dir) { m_moduleDirectory = dir; }
    const std::filesystem::path& GetModuleDirectory() const { return m_moduleDirectory; }

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    // Mid-frame pass interception: called when the main geometry pass completes, before post-processing / UI.
    // Runs the reconstruction AT MOST ONCE per frame: extra matching passes in the same frame
    // (reflections, mirrors, cubemaps) are ignored, so history ping-pong cannot desynchronize.
    void OnScenePassEnd(void* cmdBufferOrContext);
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent(void* presentTarget);
    // Call when the game (re)creates its swapchain: resets frame parity, history, and updates dimensions if provided
    void OnSwapchainRecreated(uint32_t width = 0, uint32_t height = 0);

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
    std::mutex                 m_hookMutex; // serializes TryInstallHooks (init thread vs CBR_PluginInit)
    std::atomic<bool>          m_initialized{ false };
    std::atomic<bool>          m_enabled{ true };
    std::atomic<uint32_t>      m_frameIndex{ 0 };
    // The present target (VkQueue / IDXGISwapChain) of the game's main output. Presents from any
    // other target (overlays, loading screens, secondary windows) must not advance checkerboard parity.
    std::atomic<void*>         m_mainPresentTarget{ nullptr };
    // Frame index for which the reconstruction last ran (kNoFrame = none yet)
    static constexpr uint32_t  kNoFrame = 0xFFFFFFFFu;
    std::atomic<uint32_t>      m_lastDispatchedFrame{ kNoFrame };
    // PreferredApi = Auto and no runtime was loaded yet: re-detect on each hook attempt
    std::atomic<bool>          m_apiPending{ false };
    std::atomic<float>         m_lastReconDurationMs{ 0.0f };
    std::atomic<GraphicsApi>   m_activeApi{ GraphicsApi::Vulkan };
    std::filesystem::path      m_moduleDirectory;
};

} // namespace cbr
```

<a id="includecbrcheckerboardmappingh"></a>
### `include/cbr/checkerboard_mapping.h`
```cpp
#pragma once

#include <cstdint>

namespace cbr {

// -----------------------------------------------------------------------------------------------
// Checkerboard sample geometry for 2x MSAA at half resolution per axis ("quarter" resolution).
//
// Standard 2x MSAA sample locations in [0,1] pixel coordinates, y DOWN (the Vulkan "standard
// sample locations", also used by D3D's standard pattern and by Metal):
//   sample 0 = (0.75, 0.75) bottom-right
//   sample 1 = (0.25, 0.25) top-left
// Sources: Mesa src/intel/common/intel_sample_positions.* ("standard multisample positions used
// by both GL and Vulkan") and Apple's MTLDevice.getDefaultSamplePositions(2). Confirm against the
// sample locations the game actually uses (VK_EXT_sample_locations / D3D12 programmable positions).
//
// One render pixel covers a 2x2 block of output ("target") pixels, so the two samples land on the
// centres of the top-left and bottom-right target pixels of that block.
//
// frameParity 0 (unjittered): samples shade target pixels with (x&1) == (y&1).
// frameParity 1 (sampling grid shifted by shiftDir full-resolution pixels in +x):
//   samples shade target pixels with (x&1) != (y&1).
//
// Across the two frames every target pixel is shaded exactly once (verified by simulation in
// tests/test_core.cpp). The jitter is a whole-pixel coverage shift, NOT sub-pixel TAA jitter: the
// mapping below already assigns each sample to the output pixel it belongs to, so history
// reprojection needs no jitter compensation.
// -----------------------------------------------------------------------------------------------

struct SampleOffset {
    float x;
    float y;
};

inline constexpr SampleOffset kStandardSample2x[2] = {
    { 0.75f, 0.75f },
    { 0.25f, 0.25f }
};

struct CheckerboardSample {
    bool     active;   // true if a native sample exists for this target pixel this frame
    int32_t  quarterX; // render-target (quarter-res) texel
    int32_t  quarterY;
    uint32_t sample;   // MSAA sample index within that texel
};

// Maps a target pixel (x, y >= 0) to the MSAA sample that shaded it.
// For inactive pixels the returned location is the (meaningless) sample that would be there if
// the pixel were active; callers must check `active`.
// quarterX may fall outside the render target at the left/right screen edge; callers must clamp.
constexpr CheckerboardSample MapPixelToSample(int32_t x, int32_t y, uint32_t frameParity, int32_t shiftDir = 1) noexcept {
    const int32_t yBit = y & 1;
    const bool active = ((static_cast<uint32_t>(x ^ y) & 1u) == (frameParity & 1u));
    int32_t qx = 0;
    if ((frameParity & 1u) == 0u) {
        qx = x >> 1;
    } else if (shiftDir >= 0) {
        // +1 px shift: the bottom-right sample of the neighbouring render pixel lands here
        qx = (x > yBit ? x - yBit : 0) >> 1;
    } else {
        // -1 px shift
        qx = (x + 1 - yBit) >> 1;
    }
    return { active, qx, y >> 1, static_cast<uint32_t>(1 - yBit) };
}

} // namespace cbr
```

<a id="includecbrconfigh"></a>
### `include/cbr/config.h`
```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <mutex>

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

enum class DepthConvention {
    Standard,
    Reversed
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
    float           depthTolerance{ 0.010f };
    bool            enableColorClamping{ true };
    ColorSpace      colorSpace{ ColorSpace::YCoCg };
    float           historyWeight{ 0.90f };
    bool            enableSpatialFallback{ true };
    // 3x3 closest-depth motion-vector dilation over active samples.
    bool            enableMotionDilation{ true };
    DepthConvention depthConvention{ DepthConvention::Reversed };
    float           depthNear{ 0.1f };
    float           depthFar{ 0.0f }; // 0.0 = infinite far plane (Reversed only)

    // Jitter
    JitterPattern jitterPattern{ JitterPattern::Checkerboard };
    float         jitterScale{ 1.0f };
    int32_t       jitterDirection{ 1 }; // +1 or -1
    // Multiplier applied to the jitter delta when reprojecting history: default 0.0
    // (whole-pixel coverage jitter is absorbed by the sample mapping, so history needs no compensation).
    float         jitterCompensation{ 0.0f };

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

    CBRConfig GetConfig() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config;
    }
    CBRConfig& GetMutableConfig() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config;
    }
    void UpdateConfig(const CBRConfig& config) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = config;
    }
    template<typename Fn>
    void Modify(Fn&& fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        fn(m_config);
    }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    mutable std::mutex m_mutex;
    CBRConfig m_config;
};

} // namespace cbr
```

<a id="includecbrdepthconventionh"></a>
### `include/cbr/depth_convention.h`
```cpp
#pragma once

#include <algorithm>
#include <cmath>
#include "cbr/config.h"

namespace cbr {

// Host-side mirror of LinearizeDepth() in shaders/cbr_reconstruct.{comp,hlsl}; keep in sync.
//
// Device depth d in [0,1] -> positive view-space distance.
// Standard (0 = near, 1 = far): z = n*f / (f - d*(f - n))
// Reversed (1 = near, 0 = far): z = n*f / (n + d*(f - n)), or n / d when f is infinite (f <= 0)
inline float LinearizeDepth(float d, DepthConvention convention, float zNear, float zFar) {
    if (convention == DepthConvention::Reversed) {
        if (zFar <= 0.0f) return zNear / std::max(d, 1e-7f);
        return (zNear * zFar) / (zNear + d * (zFar - zNear));
    }
    return (zNear * zFar) / (zFar - d * (zFar - zNear));
}

// Relative (scale-invariant) depth difference used by the disocclusion test.
inline float RelativeDepthDelta(float zA, float zB) {
    return std::fabs(zA - zB) / std::max(zA, 1e-5f);
}

// Depth range actually sent to the GPU. Standard depth needs a finite far plane; a missing or
// invalid one falls back to a large finite value so the shader never divides by zero.
struct DepthRange {
    float zNear;
    float zFar;
};

inline DepthRange SanitizeDepthRange(DepthConvention convention, float zNear, float zFar) {
    DepthRange r{ zNear > 0.0f ? zNear : 0.1f, zFar };
    if (convention == DepthConvention::Standard && !(r.zFar > r.zNear)) {
        r.zFar = 1000.0f;
    }
    if (r.zFar < 0.0f) r.zFar = 0.0f;
    return r;
}

} // namespace cbr
```

<a id="includecbrhooksh"></a>
### `include/cbr/hooks.h`
```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include "cbr/config.h"

namespace cbr {

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    // Picks the graphics API whose runtime is already loaded in the host process (Vulkan preferred).
    // Returns GraphicsApi::Auto when neither runtime is loaded yet.
    GraphicsApi DetectLoadedApi() const;

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

    // Idempotent: sets jitter on outMatrix4x4 relative to inUnjitteredMatrix4x4 without accumulating.
    // IMPORTANT: inUnjitteredMatrix4x4 must really be the unjittered source. Passing the same pointer
    // for both and calling twice re-applies the offset (use a separate output buffer).
    void SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const;

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
    uint32_t colorSpace;            // 0 = YCoCg, 1 = RGB
    uint32_t enableSpatialFallback; // 1 = on
    float    jitterDelta[2];        // subpixel projection jitter delta (jc - jp)
    uint32_t enableMotionDilation;  // 1 = 3x3 closest-depth motion dilation
    float    jitterCompensation;    // multiplier on jitterDelta (default 0: not needed, see checkerboard_mapping.h)
    int32_t  shiftDirection;        // odd-frame sampling-grid shift: +1 / -1 (see checkerboard_mapping.h)
    uint32_t depthMode;             // 0 = Standard, 1 = Reversed
    float    depthNear;
    float    depthFar;              // 0 = infinite (Reversed only)
};

// Must match the push-constant block / cbuffer in shaders/cbr_reconstruct.{comp,hlsl}
static_assert(sizeof(ReconstructionPushConstants) == 80, "push constant layout drifted from the shaders");
static_assert(sizeof(ReconstructionPushConstants) % 16 == 0, "cbuffer size must be a multiple of 16 bytes");

// Builds the push-constant / cbuffer payload from the current config, jitter state and target size.
// Shared by the Vulkan and DX12 dispatch paths so they cannot diverge.
ReconstructionPushConstants BuildReconstructionPushConstants(uint32_t frameIndex);

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
    bool IsQuarterPassCandidate(uint32_t width, uint32_t height) const;

    // Ping-pong history buffer index management
    uint32_t GetCurrentHistoryIndex() const { return m_historyPingPong.load(); }
    uint32_t GetPreviousHistoryIndex() const { return 1u - m_historyPingPong.load(); }
    void     SwapHistoryBuffers() { m_historyPingPong.fetch_xor(1u); }
    void     ResetHistory() { m_historyPingPong.store(0u); }

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
    // Config, logging and buffers do not depend on the graphics runtime, so set up immediately.
    auto& engine = cbr::CBREngine::Get();
    if (!engine.Initialize()) {
        return 0;
    }

    // Retry the hook installation itself (not just the trigger): the runtime for the configured
    // API may load well after this plugin. Poll every 250 ms for up to 60 s, then give up quietly.
    constexpr DWORD kIntervalMs = 250;
    constexpr DWORD kMaxAttempts = 240; // 240 * 250 ms = 60 seconds

    for (DWORD attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (engine.TryInstallHooks()) {
            CBR_LOG_INFO("Graphics hooks installed after %lu attempt(s).", static_cast<unsigned long>(attempt + 1));
            return 0;
        }
        Sleep(kIntervalMs);
    }

    CBR_LOG_WARN("Graphics hooks could not be installed within 60 s; CBR stays inactive.");
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
    cbr::CBREngine::Get().TryInstallHooks();
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
        GraphicsApi api = config.preferredApi;
        if (api == GraphicsApi::Auto) {
            const GraphicsApi detected = HookManager::Get().DetectLoadedApi();
            if (detected == GraphicsApi::Auto) {
                // No runtime loaded yet: default to Vulkan for now and re-detect on every hook attempt
                api = GraphicsApi::Vulkan;
                m_apiPending.store(true);
                CBR_LOG_INFO("PreferredApi=Auto: no graphics runtime loaded yet; will re-detect.");
            } else {
                api = detected;
                CBR_LOG_INFO("PreferredApi=Auto resolved to %s.", api == GraphicsApi::Vulkan ? "Vulkan" : "D3D12");
            }
        }
        m_activeApi.store(api);

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Prepare the reconstruction pass for the active API
        HookManager::Get().Initialize();
        if (api == GraphicsApi::Vulkan) {
            ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
        } else {
            ReconstructionPass::Get().InitializeDX12(nullptr);
        }

        m_initialized.store(true);
        CBR_LOG_INFO("CBREngine initialized successfully. Waiting for graphics hooks.");
    });

    return m_initialized.load();
}

bool CBREngine::TryInstallHooks() {
    if (!m_initialized.load()) return false;

    std::lock_guard<std::mutex> lock(m_hookMutex);

    // Auto mode with no runtime at init time: pick whichever runtime has appeared since
    if (m_apiPending.load()) {
        const GraphicsApi detected = HookManager::Get().DetectLoadedApi();
        if (detected == GraphicsApi::Auto) return false; // still nothing to hook
        if (detected != m_activeApi.load()) {
            m_activeApi.store(detected);
            if (detected == GraphicsApi::Vulkan) ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
            else                                 ReconstructionPass::Get().InitializeDX12(nullptr);
        }
        m_apiPending.store(false);
        CBR_LOG_INFO("PreferredApi=Auto resolved to %s.", detected == GraphicsApi::Vulkan ? "Vulkan" : "D3D12");
    }

    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        return HookManager::Get().IsVulkanHooked() || HookManager::Get().InstallVulkanHooks();
    }
    return HookManager::Get().IsDX12Hooked() || HookManager::Get().InstallDX12Hooks();
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

void CBREngine::OnScenePassEnd(void* cmdBufferOrContext) {
    if (!m_enabled.load()) return;

    const uint32_t currentFrame = m_frameIndex.load();

    // At most one reconstruction (and one history swap) per frame. exchange() makes this race-free
    // if the hook ever fires from more than one thread.
    if (m_lastDispatchedFrame.exchange(currentFrame) == currentFrame) return;

    // Mid-frame dispatch: reconstruct immediately when the quarter-resolution 2x MSAA
    // geometry pass finishes, before post-processing and UI are composited.
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(cmdBufferOrContext, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(cmdBufferOrContext, currentFrame);
    }

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();
}

void CBREngine::OnPrePresent(void* queueOrSwapchain, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    // Only the main output may handle presentation callbacks
    void* mainTarget = m_mainPresentTarget.load();
    if (mainTarget != nullptr && queueOrSwapchain != mainTarget) return;

    // Render ImGui overlay if toggled on (Present is the correct timing for overlay drawing)
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent(void* presentTarget) {
    // The first present seen after start-up / swapchain recreation defines the main target.
    void* expected = nullptr;
    m_mainPresentTarget.compare_exchange_strong(expected, presentTarget);

    if (m_mainPresentTarget.load() == presentTarget) {
        m_frameIndex.fetch_add(1);
    }
}

void CBREngine::OnSwapchainRecreated(uint32_t width, uint32_t height) {
    m_mainPresentTarget.store(nullptr);
    m_frameIndex.store(0);
    m_lastDispatchedFrame.store(kNoFrame);

    // Swapchain extents are untrusted: odd sizes round up to even, implausible ones (< 320 or > 16384) are ignored
    constexpr uint32_t kMinExtent = 320;
    constexpr uint32_t kMaxExtent = 16384;
    if (width >= kMinExtent && width <= kMaxExtent && height >= 240 && height <= kMaxExtent) {
        if (width & 1u) ++width;
        if (height & 1u) ++height;
        RenderTargetManager::Get().Initialize(width, height);
        JitterManager::Get().Initialize(width, height);
        CBR_LOG_INFO("Swapchain recreated with new resolution %ux%u: frame parity and history reset.", width, height);
    } else {
        RenderTargetManager::Get().ResetHistory();
        CBR_LOG_INFO("Swapchain recreated: frame parity and history reset.");
    }
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
```

<a id="srchookscpp"></a>
### `src/hooks.cpp`
```cpp
#include "cbr/hooks.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cbr {

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

GraphicsApi HookManager::DetectLoadedApi() const {
#if defined(_WIN32)
    if (GetModuleHandleA("vulkan-1.dll")) return GraphicsApi::Vulkan;
    if (GetModuleHandleA("d3d12.dll"))    return GraphicsApi::D3D12;
#endif
    return GraphicsApi::Auto; // neither runtime is loaded (yet)
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
#include <atomic>
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

// Minimal Vulkan struct layouts for headerless extraction of swapchain and extent
struct MinimalVkExtent2D {
    uint32_t width;
    uint32_t height;
};

struct MinimalVkSwapchainCreateInfoKHR {
    uint32_t          sType;
    const void*       pNext;
    uint32_t          flags;
    uint64_t          surface;
    uint32_t          minImageCount;
    int32_t           imageFormat;
    int32_t           imageColorSpace;
    MinimalVkExtent2D imageExtent;
};

struct MinimalVkPresentInfoKHR {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     waitSemaphoreCount;
    const void*  pWaitSemaphores;
    uint32_t     swapchainCount;
    const void** pSwapchains;
    const uint32_t* pImageIndices;
    int*         pResults;
};

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return kVkErrorInitializationFailed;
    }

    void* presentTarget = queue;
    if (pPresentInfo) {
        const auto* info = reinterpret_cast<const MinimalVkPresentInfoKHR*>(pPresentInfo);
        if (info->swapchainCount > 0 && info->pSwapchains) {
            presentTarget = const_cast<void*>(info->pSwapchains[0]);
        }
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(presentTarget, pPresentInfo);
    } catch (...) {
    }

    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent(presentTarget);
    } catch (...) {
    }
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    if (!g_Original_vkCreateSwapchainKHR) {
        return kVkErrorInitializationFailed;
    }
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    const int result = g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (result == 0) { // VK_SUCCESS
        uint32_t w = 0, h = 0;
        if (pCreateInfo) {
            const auto* info = reinterpret_cast<const MinimalVkSwapchainCreateInfoKHR*>(pCreateInfo);
            w = info->imageExtent.width;
            h = info->imageExtent.height;
        }
        try { CBREngine::Get().OnSwapchainRecreated(w, h); } catch (...) {}
    }
    return result;
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
    // Logged once only: this function is retried from a polling loop.
    static std::atomic<bool> s_warned{ false };
    if (!s_warned.exchange(true)) {
        CBR_LOG_WARN("Vulkan hook installation is not implemented yet; no hooks are active.");
    }
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
#include <atomic>
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
        CBREngine::Get().OnPostPresent(swapChain);
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
    // Logged once only: this function is retried from a polling loop.
    static std::atomic<bool> s_warned{ false };
    if (!s_warned.exchange(true)) {
        CBR_LOG_WARN("DX12 hook installation is not implemented yet; no hooks are active.");
    }
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
#include "cbr/config.h"
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

    // 2-phase checkerboard jitter sequence:
    // Even frames are unjittered (samples land on pixel centres).
    // Odd frames are shifted horizontally by exactly one full-resolution pixel (shiftDirection * pixelWidth),
    // giving 100% 4-quadrant geometric coverage across two frames with standard 2x MSAA sample locations.
    const auto& config = ConfigManager::Get().GetConfig();
    const float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;

    if (frameIndex & 1u) {
        m_currentJitter.x = dir * pixelWidth;
        m_currentJitter.y = 0.0f;
    } else {
        m_currentJitter.x = 0.0f;
        m_currentJitter.y = 0.0f;
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

void JitterManager::SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const {
    if (!outMatrix4x4 || !inUnjitteredMatrix4x4) return;

    if (outMatrix4x4 != inUnjitteredMatrix4x4) {
        for (int i = 0; i < 16; ++i) {
            outMatrix4x4[i] = inUnjitteredMatrix4x4[i];
        }
    }

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    outMatrix4x4[8] = inUnjitteredMatrix4x4[8] + jitterNdcX;
    outMatrix4x4[9] = inUnjitteredMatrix4x4[9] + jitterNdcY;
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
    m_disabled = true;
    m_pending.clear();
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
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
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

ReconstructionPushConstants BuildReconstructionPushConstants(uint32_t frameIndex) {
    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();
    const auto jitterDelta = JitterManager::Get().GetJitterDelta();

    ReconstructionPushConstants pc{};
    pc.targetResolution[0] = static_cast<float>(dims.fullWidth);
    pc.targetResolution[1] = static_cast<float>(dims.fullHeight);
    pc.invTargetResolution[0] = 1.0f / pc.targetResolution[0];
    pc.invTargetResolution[1] = 1.0f / pc.targetResolution[1];
    pc.frameIndex = frameIndex;
    pc.depthTolerance = config.depthTolerance;
    pc.historyWeight = config.historyWeight;
    pc.debugView = config.debugView;
    pc.enableColorClamping = config.enableColorClamping ? 1u : 0u;
    pc.mipLodBias = config.mipLodBias;
    pc.colorSpace = (config.colorSpace == ColorSpace::RGB) ? 1u : 0u;
    pc.enableSpatialFallback = config.enableSpatialFallback ? 1u : 0u;
    pc.jitterDelta[0] = jitterDelta.x;
    pc.jitterDelta[1] = jitterDelta.y;
    pc.enableMotionDilation = config.enableMotionDilation ? 1u : 0u;
    pc.jitterCompensation = config.jitterCompensation;
    pc.shiftDirection = (config.jitterDirection < 0) ? -1 : 1;
    const DepthRange range = SanitizeDepthRange(config.depthConvention, config.depthNear, config.depthFar);
    pc.depthMode = (config.depthConvention == DepthConvention::Reversed) ? 1u : 0u;
    pc.depthNear = range.zNear;
    pc.depthFar = range.zFar;
    return pc;
}

void ReconstructionPass::DispatchVulkan(void* /*vkCommandBuffer*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    const uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In a live Vulkan context, this binds the compute pipeline, pushes `pushConstants`,
    // and calls vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);
    // Followed by a memory barrier transitioning the reconstructed image for sampling.
    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
}

void ReconstructionPass::DispatchDX12(void* /*d3d12GraphicsCommandList*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    const uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In DX12, sets root signature, pipeline state, descriptor tables, uploads `pushConstants`
    // as root constants, and calls Dispatch(groupCountX, groupCountY, 1)
    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
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
    // 4. Full-Res Depth History A & B (R32F = 4 bytes * 2 buffers):
    size_t histDepth = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 4 * 2;
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

bool RenderTargetManager::IsQuarterPassCandidate(uint32_t width, uint32_t height) const {
    if (!m_initialized) return false;
    return (width == m_dims.quarterWidth && height == m_dims.quarterHeight);
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
 * Architecture: Optimized for NVIDIA Pascal (GP104 / Wave32) & AMD Radeon Vega (GCN 5.0 / Wave64)
 * Target: Vulkan SPIR-V
 * Author & Co-Owner: Shreyas Pawar
 */

// 16x16 = 256 threads per workgroup, an exact multiple of both wave sizes (no partial waves):
// - NVIDIA Pascal (GP104): 8 warps x 32 threads
// - AMD Radeon Vega (GCN 5.0 / Vega 7): 4 wavefronts x 64 threads
// (Achieved occupancy additionally depends on register and LDS usage; measure with a profiler.)
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

// Full-resolution depth written for the NEXT frame's disocclusion test (becomes u_HistoryDepth)
layout(set = 0, binding = 6, r32f) writeonly uniform image2D u_OutputDepth;

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
    uint  u_ColorSpace;             // 0 = YCoCg clamp, 1 = RGB clamp
    uint  u_EnableSpatialFallback;  // 1 = cross-bilateral fallback, 0 = use raw current sample
    vec2  u_JitterDelta;            // Subpixel projection jitter delta (jc - jp)
        uint      u_EnableMotionDilation;  // 1 = closest-depth motion dilation over the active 3x3 neighbours
    float     u_JitterCompensation;    // Multiplier on u_JitterDelta (default 0: whole-pixel jitter is absorbed by the mapping)
    int       u_ShiftDirection;        // Odd-frame sampling-grid shift: +1 / -1 (include/cbr/checkerboard_mapping.h)
    uint      u_DepthMode;             // 0 = Standard (0 near .. 1 far), 1 = Reversed (1 near .. 0 far)
    float     u_DepthNear;             // Camera near plane
    float     u_DepthFar;              // Camera far plane (0 = infinite, Reversed only)
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

vec3 ToClampSpace(vec3 rgb)  { return (pc.u_ColorSpace == 0u) ? RGBtoYCoCg(rgb) : rgb; }
vec3 FromClampSpace(vec3 c)  { return (pc.u_ColorSpace == 0u) ? YCoCgtoRGB(c) : max(vec3(0.0), c); }

// =============================================================================
// Checkerboard sample mapping. MIRRORS include/cbr/checkerboard_mapping.h: keep the two in sync
// (the C++ version is verified against a geometric simulation in tests/test_core.cpp).
//
// Standard 2x MSAA sample locations (y down): sample 0 = (0.75, 0.75) bottom-right,
// sample 1 = (0.25, 0.25) top-left. Even frames shade pixels with (x&1)==(y&1); odd frames (grid shifted
// by one full-resolution pixel in x) shade pixels with (x&1)!=(y&1).
// =============================================================================
struct CbrSample {
    bool  isActive;  // a native sample exists for this pixel this frame
    ivec2 quarter;   // quarter-resolution texel (already clamped to the render target)
    int   sampleIdx; // MSAA sample index
};

CbrSample MapPixelToSample(ivec2 p, uint frameParity) {
    CbrSample s;
    int yBit = p.y & 1;
    s.isActive = (((uint(p.x) ^ uint(p.y)) & 1u) == frameParity);
    int qx;
    if (frameParity == 0u) {
        qx = p.x >> 1;
    } else if (pc.u_ShiftDirection >= 0) {
        qx = max(p.x - yBit, 0) >> 1;
    } else {
        qx = (p.x + 1 - yBit) >> 1;
    }
    ivec2 qMax = ivec2(pc.u_TargetResolution * 0.5) - ivec2(1);
    s.quarter = ivec2(clamp(qx, 0, qMax.x), min(p.y >> 1, qMax.y));
    s.sampleIdx = 1 - yBit;
    return s;
}

vec3 FetchColor(CbrSample s) {
    return texelFetch(u_QuarterColorMSAA, s.quarter, s.sampleIdx).rgb;
}

float FetchDepth(CbrSample s) {
    return texelFetch(u_QuarterDepthMSAA, s.quarter, s.sampleIdx).r;
}

// MIRRORS include/cbr/depth_convention.h
float LinearizeDepth(float d) {
    float n = pc.u_DepthNear;
    float f = pc.u_DepthFar;
    if (pc.u_DepthMode == 1u) {
        return (f > 0.0) ? (n * f) / (n + d * (f - n)) : n / max(d, 1e-7);
    }
    return (n * f) / (f - d * (f - n));
}

bool IsNearer(float a, float b) {
    return (pc.u_DepthMode == 1u) ? (a > b) : (a < b);
}

bool InBounds(ivec2 p, ivec2 size) {
    return p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y;
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
    uint frameParity = pc.u_FrameIndex & 1u;
    const ivec2 kCardinal[4] = ivec2[](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));

    // -------------------------------------------------------------------------
    // 1. Current-frame sample (native if this pixel was shaded this frame)
    // -------------------------------------------------------------------------
    CbrSample self = MapPixelToSample(pixelCoord, frameParity);
    bool isCurrentSampleActive = self.isActive;
    vec3 currentColor = vec3(0.0); // valid only for active pixels
    float currentDepth;            // raw device depth (what gets written to the history depth target)
    float currentLinear;           // linear depth used by the occlusion test

    // The 4 cardinal neighbours of a reconstructed pixel are always natively shaded (mirrored at borders).
    ivec2 cardinalCoord[4] = ivec2[](ivec2(0), ivec2(0), ivec2(0), ivec2(0));
    float cardinalDepth[4] = float[](0.0, 0.0, 0.0, 0.0);

    if (isCurrentSampleActive) {
        currentColor = FetchColor(self);
        currentDepth = FetchDepth(self);
        currentLinear = LinearizeDepth(currentDepth);
    } else {
        // No native sample here: estimate depth from the cardinal neighbours (Intel "CSO": average LINEAR depth).
        float sumRaw = 0.0;
        float sumLin = 0.0;
        for (int i = 0; i < 4; ++i) {
            ivec2 c = pixelCoord + kCardinal[i];
            if (!InBounds(c, targetSize)) {
                c = pixelCoord - kCardinal[i]; // mirror at the screen border
            }
            cardinalCoord[i] = c;
            cardinalDepth[i] = FetchDepth(MapPixelToSample(c, frameParity));
            sumRaw += cardinalDepth[i];
            sumLin += LinearizeDepth(cardinalDepth[i]);
        }
        currentDepth = sumRaw * 0.25;
        currentLinear = sumLin * 0.25;
    }

    // -------------------------------------------------------------------------
    // 2. Motion vector (optionally dilated to the nearest-depth neighbour) & history UV
    // -------------------------------------------------------------------------
    // Only natively shaded neighbours carry depth, so the dilation scans just those (5 of the 3x3 around an
    // active pixel, 4 around a reconstructed one). Skipped entirely when disabled.
    ivec2 motionCoord = pixelCoord;
    if (pc.u_EnableMotionDilation != 0u) {
        bool found = false;
        float best = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 n = pixelCoord + ivec2(dx, dy);
                if (!InBounds(n, targetSize)) continue;
                CbrSample ns = MapPixelToSample(n, frameParity);
                if (!ns.isActive) continue;
                float d = FetchDepth(ns);
                if (!found || IsNearer(d, best)) {
                    found = true;
                    best = d;
                    motionCoord = n;
                }
            }
        }
    }

    vec2 dilatedUV = (vec2(motionCoord) + 0.5) * pc.u_InvTargetResolution;
    vec2 velocity = textureLod(u_Velocity, dilatedUV, 0.0).xy;
    vec2 historyUV = uv - velocity - pc.u_JitterDelta * pc.u_JitterCompensation;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & depth delta testing (scale-invariant: relative difference of LINEAR depth)
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    vec4 historyColor = vec4(0.0);
    float previousDepth = currentDepth;

    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0) {
        isDisoccluded = true; // Sample moved outside screen space
    } else {
        // Exact texel fetch: independent of the bound sampler, so depth is never blended across edges
        ivec2 historyCoord = clamp(ivec2(historyUV * pc.u_TargetResolution), ivec2(0), targetSize - ivec2(1));
        previousDepth = texelFetch(u_HistoryDepth, historyCoord, 0).r;
        float depthDelta = abs(currentLinear - LinearizeDepth(previousDepth)) / max(currentLinear, 1e-5);
        if (depthDelta > pc.u_DepthTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = textureLod(u_HistoryColor, historyUV, 0.0);
            if (historyColor.a <= 0.0) {
                isDisoccluded = true; // First frame or cleared history buffer
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighbourhood statistics & variance clipping (anti-ghosting)
    // Uses ONLY natively shaded neighbours: samples that do not exist this frame must not enter the statistics.
    // -------------------------------------------------------------------------
    if (!isDisoccluded && pc.u_EnableColorClamping != 0u) {
        vec3 colorMin = vec3(1e6);
        vec3 colorMax = vec3(-1e6);
        vec3 m1 = vec3(0.0);
        vec3 m2 = vec3(0.0);
        float n = 0.0;

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 nc = pixelCoord + ivec2(dx, dy);
                if (!InBounds(nc, targetSize)) continue;
                CbrSample ns = MapPixelToSample(nc, frameParity);
                if (!ns.isActive) continue;

                vec3 c = ToClampSpace(FetchColor(ns));
                colorMin = min(colorMin, c);
                colorMax = max(colorMax, c);
                m1 += c;
                m2 += c * c;
                n += 1.0;
            }
        }

        if (n >= 2.0) {
            // Variance clipping: clamp history inside [mean - gamma * stdDev, mean + gamma * stdDev]
            vec3 mean = m1 / n;
            vec3 stdDev = sqrt(max(vec3(0.0), (m2 / n) - (mean * mean)));
            float gamma = 1.25;
            vec3 varianceMin = max(colorMin, mean - gamma * stdDev);
            vec3 varianceMax = min(colorMax, mean + gamma * stdDev);

            vec3 historyClampSpace = ToClampSpace(historyColor.rgb);
            historyClampSpace = clamp(historyClampSpace, varianceMin, varianceMax);
            historyColor.rgb = FromClampSpace(historyClampSpace);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Final pixel reconstruction
    // -------------------------------------------------------------------------
    vec3 finalColor;
    float spatialDepth = currentDepth; // depth estimate for reconstructed pixels (fallback path)

    if (isCurrentSampleActive) {
        if (!isDisoccluded && historyColor.a > 0.0) {
            // Blend with history for temporal stability (u_HistoryWeight controls history influence)
            finalColor = mix(currentColor, historyColor.rgb, pc.u_HistoryWeight);
        } else {
            finalColor = currentColor;
        }
    } else {
        if (!isDisoccluded && historyColor.a > 0.0) {
            finalColor = historyColor.rgb;
        } else {
            // Spatial cross-bilateral reconstruction from the 4 cardinal (natively shaded) neighbours.
            // With the fallback disabled the nearest neighbour is copied (no native sample exists here).
            vec3 accumColor = vec3(0.0);
            float accumDepth = 0.0;
            float accumWeight = 0.0;

            for (int i = 0; i < 4 && pc.u_EnableSpatialFallback != 0u; ++i) {
                vec3 sCol = FetchColor(MapPixelToSample(cardinalCoord[i], frameParity));
                float sLin = LinearizeDepth(cardinalDepth[i]);

                // Relative linear-depth weighting: scale-invariant, independent of the depth convention
                float depthWeight = exp(-abs(currentLinear - sLin) / (max(currentLinear, 1e-5) * max(pc.u_DepthTolerance, 1e-4)));

                accumColor += sCol * depthWeight;
                accumDepth += cardinalDepth[i] * depthWeight;
                accumWeight += depthWeight;
            }

            if (accumWeight > 1e-4) {
                finalColor = accumColor / accumWeight;
                spatialDepth = accumDepth / accumWeight;
            } else {
                finalColor = FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
                spatialDepth = cardinalDepth[0];
            }
        }
    }

    // Guard against NaN/Inf pollution in the temporal feedback loop
    if (isnan(finalColor.r) || isinf(finalColor.r) ||
        isnan(finalColor.g) || isinf(finalColor.g) ||
        isnan(finalColor.b) || isinf(finalColor.b)) {
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    }

    // -------------------------------------------------------------------------
    // 6. Debug visualization modes
    // -------------------------------------------------------------------------
    if (pc.u_DebugView == 1u) {
        // Checkerboard mask: White = natively shaded this frame, Black = reconstructed
        finalColor = isCurrentSampleActive ? vec3(1.0, 1.0, 1.0) : vec3(0.05, 0.05, 0.05);
    } else if (pc.u_DebugView == 2u) {
        // Disocclusion heatmap: Green = temporal history, Red = spatial disocclusion
        finalColor = isDisoccluded ? vec3(1.0, 0.1, 0.1) : vec3(0.1, 0.9, 0.1);
    } else if (pc.u_DebugView == 3u) {
        // Motion vector field
        finalColor = vec3(abs(velocity) * 50.0, 0.0);
    } else if (pc.u_DebugView == 4u) {
        // Quarter-resolution raw colour (nearest native sample for reconstructed pixels)
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    }

    // Write final reconstructed pixel to output storage image (alpha = 1.0 for valid history)
    imageStore(u_OutputImage, pixelCoord, vec4(finalColor, 1.0));

    // History depth for the next frame: native depth for shaded pixels; for reconstructed pixels
    // carry the validated history depth, or the spatial estimate when history was rejected.
    float outDepth = currentDepth;
    if (!isCurrentSampleActive) {
        outDepth = isDisoccluded ? spatialDepth : previousDepth;
    }
    imageStore(u_OutputDepth, pixelCoord, vec4(outDepth, 0.0, 0.0, 0.0));
}
```

<a id="shaderscbrreconstructhlsl"></a>
### `shaders/cbr_reconstruct.hlsl`
```hlsl
/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / Wave32) & AMD Radeon Vega (GCN 5.0 / Wave64)
 * Target: DirectX 12 HLSL (CS 5.0 / CS 6.0)
 * Author & Co-Owner: Shreyas Pawar
 */

// 16x16 = 256 threads per threadgroup, an exact multiple of both wave sizes (no partial waves):
// - NVIDIA Pascal (GP104): 8 warps x 32 threads
// - AMD Radeon Vega (GCN 5.0 / Vega 7): 4 wavefronts x 64 threads
// (Achieved occupancy additionally depends on register and LDS usage; measure with a profiler.)
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
// Full-resolution depth written for the next frame's disocclusion test (becomes g_HistoryDepth)
RWTexture2D<float>  g_OutputDepth      : register(u1);

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
    uint   g_ColorSpace;             // 0 = YCoCg clamp, 1 = RGB clamp
    uint   g_EnableSpatialFallback;  // 1 = cross-bilateral fallback, 0 = raw current sample
    float2 g_JitterDelta;            // subpixel projection jitter delta (jc - jp)
        uint   g_EnableMotionDilation;  // 1 = closest-depth motion dilation over the active 3x3 neighbours
    float  g_JitterCompensation;    // multiplier on g_JitterDelta (default 0: whole-pixel jitter is absorbed by the mapping)
    int    g_ShiftDirection;        // odd-frame sampling-grid shift: +1 / -1 (include/cbr/checkerboard_mapping.h)
    uint   g_DepthMode;             // 0 = Standard (0 near .. 1 far), 1 = Reversed (1 near .. 0 far)
    float  g_DepthNear;             // camera near plane
    float  g_DepthFar;              // camera far plane (0 = infinite, Reversed only)
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

float3 ToClampSpace(float3 rgb)   { return (g_ColorSpace == 0u) ? RGBtoYCoCg(rgb) : rgb; }
float3 FromClampSpace(float3 c)   { return (g_ColorSpace == 0u) ? YCoCgtoRGB(c) : max(float3(0.0f, 0.0f, 0.0f), c); }

// =============================================================================
// Checkerboard sample mapping. MIRRORS include/cbr/checkerboard_mapping.h: keep the two in sync
// (the C++ version is verified against a geometric simulation in tests/test_core.cpp).
//
// Standard 2x MSAA sample locations (y down): sample 0 = (0.75, 0.75) bottom-right,
// sample 1 = (0.25, 0.25) top-left. Even frames shade pixels with (x&1)==(y&1); odd frames (grid shifted
// by one full-resolution pixel in x) shade pixels with (x&1)!=(y&1).
// =============================================================================
struct CbrSample {
    bool isActive;  // a native sample exists for this pixel this frame
    int2 quarter;   // quarter-resolution texel (already clamped to the render target)
    int  sampleIdx; // MSAA sample index
};

CbrSample MapPixelToSample(int2 p, uint frameParity) {
    CbrSample s;
    int yBit = p.y & 1;
    s.isActive = (((uint(p.x) ^ uint(p.y)) & 1u) == frameParity);
    int qx;
    if (frameParity == 0u) {
        qx = p.x >> 1;
    } else if (g_ShiftDirection >= 0) {
        qx = max(p.x - yBit, 0) >> 1;
    } else {
        qx = (p.x + 1 - yBit) >> 1;
    }
    int2 qMax = int2(g_TargetResolution * 0.5f) - int2(1, 1);
    s.quarter = int2(clamp(qx, 0, qMax.x), min(p.y >> 1, qMax.y));
    s.sampleIdx = 1 - yBit;
    return s;
}

float3 FetchColor(CbrSample s) {
    return g_QuarterColorMSAA.Load(s.quarter, s.sampleIdx).rgb;
}

float FetchDepth(CbrSample s) {
    return g_QuarterDepthMSAA.Load(s.quarter, s.sampleIdx).r;
}

// MIRRORS include/cbr/depth_convention.h
float LinearizeDepth(float d) {
    float n = g_DepthNear;
    float f = g_DepthFar;
    if (g_DepthMode == 1u) {
        return (f > 0.0f) ? (n * f) / (n + d * (f - n)) : n / max(d, 1e-7f);
    }
    return (n * f) / (f - d * (f - n));
}

bool IsNearer(float a, float b) {
    return (g_DepthMode == 1u) ? (a > b) : (a < b);
}

bool InBounds(int2 p, int2 size) {
    return p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y;
}

// =============================================================================
// Compute Shader Entry Point
// =============================================================================
[numthreads(THREADGROUP_SIZE_X, THREADGROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
    int2 pixelCoord = int2(dispatchThreadId.xy);
    int2 targetSize = int2(g_TargetResolution);

    // Bounds check
    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5f) * g_InvTargetResolution;
    uint frameParity = g_FrameIndex & 1u;
    static const int2 kCardinal[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

    // -------------------------------------------------------------------------
    // 1. Current-frame sample (native if this pixel was shaded this frame)
    // -------------------------------------------------------------------------
    CbrSample self = MapPixelToSample(pixelCoord, frameParity);
    bool isCurrentSampleActive = self.isActive;
    float3 currentColor = float3(0.0f, 0.0f, 0.0f); // valid only for active pixels
    float currentDepth;                              // raw device depth (written to the history depth target)
    float currentLinear;                             // linear depth used by the occlusion test

    // The 4 cardinal neighbours of a reconstructed pixel are always natively shaded (mirrored at borders).
    int2 cardinalCoord[4] = { int2(0, 0), int2(0, 0), int2(0, 0), int2(0, 0) };
    float cardinalDepth[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (isCurrentSampleActive) {
        currentColor = FetchColor(self);
        currentDepth = FetchDepth(self);
        currentLinear = LinearizeDepth(currentDepth);
    } else {
        // No native sample here: estimate depth from the cardinal neighbours (Intel "CSO": average LINEAR depth).
        float sumRaw = 0.0f;
        float sumLin = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int2 c = pixelCoord + kCardinal[i];
            if (!InBounds(c, targetSize)) {
                c = pixelCoord - kCardinal[i]; // mirror at the screen border
            }
            cardinalCoord[i] = c;
            cardinalDepth[i] = FetchDepth(MapPixelToSample(c, frameParity));
            sumRaw += cardinalDepth[i];
            sumLin += LinearizeDepth(cardinalDepth[i]);
        }
        currentDepth = sumRaw * 0.25f;
        currentLinear = sumLin * 0.25f;
    }

    // -------------------------------------------------------------------------
    // 2. Motion vector (optionally dilated to the nearest-depth neighbour) & history UV
    // -------------------------------------------------------------------------
    // Only natively shaded neighbours carry depth, so the dilation scans just those (5 of the 3x3 around an
    // active pixel, 4 around a reconstructed one). Skipped entirely when disabled.
    int2 motionCoord = pixelCoord;
    if (g_EnableMotionDilation != 0u) {
        bool found = false;
        float best = 0.0f;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int2 n = pixelCoord + int2(dx, dy);
                if (!InBounds(n, targetSize)) continue;
                CbrSample ns = MapPixelToSample(n, frameParity);
                if (!ns.isActive) continue;
                float d = FetchDepth(ns);
                if (!found || IsNearer(d, best)) {
                    found = true;
                    best = d;
                    motionCoord = n;
                }
            }
        }
    }

    float2 dilatedUV = (float2(motionCoord) + 0.5f) * g_InvTargetResolution;
    float2 velocity = g_Velocity.SampleLevel(g_LinearClampSampler, dilatedUV, 0.0f).xy;
    float2 historyUV = uv - velocity - g_JitterDelta * g_JitterCompensation;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & depth delta test (scale-invariant: relative difference of LINEAR depth)
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float previousDepth = currentDepth;

    if (historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f) {
        isDisoccluded = true; // Sample moved outside screen space
    } else {
        previousDepth = g_HistoryDepth.SampleLevel(g_PointClampSampler, historyUV, 0.0f).r;
        float depthDelta = abs(currentLinear - LinearizeDepth(previousDepth)) / max(currentLinear, 1e-5f);
        if (depthDelta > g_DepthTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
            if (historyColor.a <= 0.0f) {
                isDisoccluded = true; // First frame or cleared history buffer
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighbourhood statistics & variance clipping (anti-ghosting)
    // Uses ONLY natively shaded neighbours: samples that do not exist this frame must not enter the statistics.
    // -------------------------------------------------------------------------
    if (!isDisoccluded && g_EnableColorClamping != 0u) {
        float3 colorMin = float3(1e6f, 1e6f, 1e6f);
        float3 colorMax = float3(-1e6f, -1e6f, -1e6f);
        float3 m1 = float3(0.0f, 0.0f, 0.0f);
        float3 m2 = float3(0.0f, 0.0f, 0.0f);
        float n = 0.0f;

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int2 nc = pixelCoord + int2(dx, dy);
                if (!InBounds(nc, targetSize)) continue;
                CbrSample ns = MapPixelToSample(nc, frameParity);
                if (!ns.isActive) continue;

                float3 c = ToClampSpace(FetchColor(ns));
                colorMin = min(colorMin, c);
                colorMax = max(colorMax, c);
                m1 += c;
                m2 += c * c;
                n += 1.0f;
            }
        }

        if (n >= 2.0f) {
            // Variance clipping: clamp history inside [mean - gamma * stdDev, mean + gamma * stdDev]
            float3 mean = m1 / n;
            float3 stdDev = sqrt(max(float3(0.0f, 0.0f, 0.0f), (m2 / n) - (mean * mean)));
            float gamma = 1.25f;
            float3 varianceMin = max(colorMin, mean - gamma * stdDev);
            float3 varianceMax = min(colorMax, mean + gamma * stdDev);

            float3 historyClampSpace = ToClampSpace(historyColor.rgb);
            historyClampSpace = clamp(historyClampSpace, varianceMin, varianceMax);
            historyColor.rgb = FromClampSpace(historyClampSpace);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Final pixel reconstruction
    // -------------------------------------------------------------------------
    float3 finalColor;
    float spatialDepth = currentDepth; // depth estimate for reconstructed pixels (fallback path)

    if (isCurrentSampleActive) {
        if (!isDisoccluded && historyColor.a > 0.0f) {
            // Blend with history for temporal stability (g_HistoryWeight controls history influence)
            finalColor = lerp(currentColor, historyColor.rgb, g_HistoryWeight);
        } else {
            finalColor = currentColor;
        }
    } else {
        if (!isDisoccluded && historyColor.a > 0.0f) {
            finalColor = historyColor.rgb;
        } else {
            // Spatial cross-bilateral reconstruction from the 4 cardinal (natively shaded) neighbours.
            // With the fallback disabled the nearest neighbour is copied (no native sample exists here).
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float accumDepth = 0.0f;
            float accumWeight = 0.0f;

            for (int i = 0; i < 4 && g_EnableSpatialFallback != 0u; ++i) {
                float3 sCol = FetchColor(MapPixelToSample(cardinalCoord[i], frameParity));
                float sLin = LinearizeDepth(cardinalDepth[i]);

                // Relative linear-depth weighting: scale-invariant, independent of the depth convention
                float depthWeight = exp(-abs(currentLinear - sLin) / (max(currentLinear, 1e-5f) * max(g_DepthTolerance, 1e-4f)));

                accumColor += sCol * depthWeight;
                accumDepth += cardinalDepth[i] * depthWeight;
                accumWeight += depthWeight;
            }

            if (accumWeight > 1e-4f) {
                finalColor = accumColor / accumWeight;
                spatialDepth = accumDepth / accumWeight;
            } else {
                finalColor = FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
                spatialDepth = cardinalDepth[0];
            }
        }
    }

    // Guard against NaN/Inf pollution in the temporal feedback loop
    if (any(isnan(finalColor)) || any(isinf(finalColor))) {
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    }

    // -------------------------------------------------------------------------
    // 6. Debug visualization modes
    // -------------------------------------------------------------------------
    if (g_DebugView == 1u) {
        // Checkerboard mask: White = natively shaded this frame, Black = reconstructed
        finalColor = isCurrentSampleActive ? float3(1.0f, 1.0f, 1.0f) : float3(0.05f, 0.05f, 0.05f);
    } else if (g_DebugView == 2u) {
        // Disocclusion heatmap: Green = temporal history, Red = spatial disocclusion
        finalColor = isDisoccluded ? float3(1.0f, 0.1f, 0.1f) : float3(0.1f, 0.9f, 0.1f);
    } else if (g_DebugView == 3u) {
        // Motion vector field
        finalColor = float3(abs(velocity) * 50.0f, 0.0f);
    } else if (g_DebugView == 4u) {
        // Quarter-resolution raw colour (nearest native sample for reconstructed pixels)
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    }

    // Write final reconstructed pixel to output storage image (alpha = 1.0 for valid history)
    g_OutputImage[pixelCoord] = float4(finalColor, 1.0f);

    // History depth for the next frame (see GLSL version for rationale)
    float outDepth = currentDepth;
    if (!isCurrentSampleActive) {
        outDepth = isDisoccluded ? spatialDepth : previousDepth;
    }
    g_OutputDepth[pixelCoord] = outDepth;
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
 *
 * Uses the same sample mapping as cbr_reconstruct.comp (see include/cbr/checkerboard_mapping.h).
 * Reconstructed pixels are the average of their 4 natively shaded cardinal neighbours.
 */

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2DMS u_QuarterColorMSAA;
layout(set = 0, binding = 1, rgba16f) writeonly uniform image2D u_OutputImage;

layout(push_constant) uniform SpatialConstants {
    vec2 u_TargetResolution;
    vec2 u_InvTargetResolution;
    uint u_FrameIndex;
    int  u_ShiftDirection; // +1 / -1, see include/cbr/checkerboard_mapping.h
} pc;

vec3 FetchMapped(ivec2 p, uint frameParity, out bool isActive) {
    int yBit = p.y & 1;
    isActive = (((uint(p.x) ^ uint(p.y)) & 1u) == frameParity);
    int qx;
    if (frameParity == 0u) {
        qx = p.x >> 1;
    } else if (pc.u_ShiftDirection >= 0) {
        qx = max(p.x - yBit, 0) >> 1;
    } else {
        qx = (p.x + 1 - yBit) >> 1;
    }
    ivec2 qMax = ivec2(pc.u_TargetResolution * 0.5) - ivec2(1);
    ivec2 quarter = ivec2(clamp(qx, 0, qMax.x), min(p.y >> 1, qMax.y));
    return texelFetch(u_QuarterColorMSAA, quarter, 1 - yBit).rgb;
}

void main() {
    ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 targetSize = ivec2(pc.u_TargetResolution);

    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y) {
        return;
    }

    uint frameParity = pc.u_FrameIndex & 1u;
    bool isActive;
    vec3 finalColor = FetchMapped(pixelCoord, frameParity, isActive);

    if (!isActive) {
        const ivec2 kCardinal[4] = ivec2[](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
        vec3 sum = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
            ivec2 c = pixelCoord + kCardinal[i];
            if (c.x < 0 || c.y < 0 || c.x >= targetSize.x || c.y >= targetSize.y) {
                c = pixelCoord - kCardinal[i]; // mirror at the screen border
            }
            bool neighbourActive;
            sum += FetchMapped(c, frameParity, neighbourActive);
        }
        finalColor = sum * 0.25;
    }

    imageStore(u_OutputImage, pixelCoord, vec4(finalColor, 1.0));
}
```

<a id="sec-tests"></a>
## 5. Tests & CI

<a id="teststestcorecpp"></a>
### `tests/test_core.cpp`
```cpp
#include "cbr/checkerboard_mapping.h"
#include "cbr/config.h"
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace cbr;

static int g_failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL: " #expr " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failures; \
    } \
} while (0)

static void WriteFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

static std::string ReadFile(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void TestConfigHardening(const fs::path& dir) {
    const fs::path ini = dir / "bad.ini";
    WriteFile(ini,
        "[General]\n"
        "TargetWidth = 4k\n"          // trailing garbage  -> default
        "TargetHeight = -1\n"         // negative          -> default
        "Enabled = true ; comment\n"  // inline comment
        "PreferredApi = Auto\n"
        "[Reconstruction]\n"
        "EnableMotionDilation = false\n"
        "DepthConvention = standard\n"   // case-insensitive
        "DepthNear = 0.5\n"
        "DepthFar = 5000\n"
        "HistoryWeight = nan\n"       // NaN               -> default
        "DepthTolerance = 99999\n"    // out of range      -> clamped
        "MipLodBias = abc\n"          // garbage           -> default
        "[Jitter]\n"
        "JitterPattern = Halton\n"    // unimplemented     -> Checkerboard
        "JitterCompensation = -5\n"   // out of range      -> clamped to -1
        "JitterDirection = -1\n"
        "JitterScale = 3\n"              // accepted but ignored (warned)
        "[Debug]\n"
        "DebugView = 77\n");          // out of range      -> clamped

    CHECK(ConfigManager::Get().Load(ini));
    const auto& c = ConfigManager::Get().GetConfig();
    CHECK(c.targetWidth == 3840);
    CHECK(c.targetHeight == 2160);
    CHECK(c.enabled);
    CHECK(c.preferredApi == GraphicsApi::Auto);
    CHECK(c.historyWeight == 0.90f);
    CHECK(c.depthTolerance == 1.0f);
    CHECK(c.mipLodBias == -0.5f);
    CHECK(c.jitterPattern == JitterPattern::Checkerboard);
    CHECK(c.debugView == 4);
    CHECK(!c.enableMotionDilation);
    CHECK(c.jitterCompensation == -1.0f);
    CHECK(c.jitterDirection == -1);
    CHECK(c.depthConvention == DepthConvention::Standard);
    CHECK(c.depthNear == 0.5f && c.depthFar == 5000.0f);

    // Defaults of a fresh config
    const CBRConfig fresh{};
    CHECK(fresh.jitterCompensation == 0.0f); // whole-pixel jitter needs NO history compensation
    CHECK(fresh.jitterDirection == 1);
    CHECK(fresh.depthConvention == DepthConvention::Reversed);

    // Invalid enum-like values keep the previous setting
    WriteFile(dir / "bad2.ini", "[Jitter]\nJitterDirection = 2\n[Reconstruction]\nDepthConvention = sideways\n");
    CHECK(ConfigManager::Get().Load(dir / "bad2.ini"));
    CHECK(ConfigManager::Get().GetConfig().jitterDirection == -1);
    CHECK(ConfigManager::Get().GetConfig().depthConvention == DepthConvention::Standard);
}

static void TestConfigMissingFileAndRoundTrip(const fs::path& dir) {
    CHECK(!ConfigManager::Get().Load(dir / "does_not_exist.ini"));

    const fs::path out = dir / "roundtrip.ini";
    CHECK(ConfigManager::Get().Save(out));
    const auto before = ConfigManager::Get().GetConfig();
    CHECK(ConfigManager::Get().Load(out));
    const auto& after = ConfigManager::Get().GetConfig();
    CHECK(after.targetWidth == before.targetWidth);
    CHECK(after.preferredApi == before.preferredApi);
    CHECK(after.historyWeight == before.historyWeight);
    CHECK(after.enableMotionDilation == before.enableMotionDilation);
    CHECK(after.jitterCompensation == before.jitterCompensation);
    CHECK(after.jitterDirection == before.jitterDirection);
    CHECK(after.depthConvention == before.depthConvention);
    CHECK(after.depthNear == before.depthNear && after.depthFar == before.depthFar);
}

static void TestLoggerBufferingAndLevel(const fs::path& dir) {
    // Messages logged before Initialize() must be buffered and flushed, honoring the min level.
    Logger::Get().SetMinLevel(LogLevel::Info);
    CBR_LOG_INFO("early info %d", 1);
    Logger::Get().SetMinLevel(LogLevel::Warning);
    CBR_LOG_INFO("filtered info");
    CBR_LOG_WARN("late warning %d", 2);

    const fs::path log = dir / "test.log";
    Logger::Get().Initialize(log);
    Logger::Get().Shutdown();

    const std::string text = ReadFile(log);
    CHECK(text.find("early info 1") != std::string::npos);   // buffered before init
    CHECK(text.find("late warning 2") != std::string::npos);
    CHECK(text.find("filtered info") == std::string::npos);  // below min level
}

static void TestJitter() {
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 1.0f; c.jitterDirection = 1; });
    auto& j = JitterManager::Get();
    j.Initialize(3840, 2160);

    j.Update(0);
    const JitterOffset even = j.GetCurrentJitter();
    j.Update(1);
    const JitterOffset odd  = j.GetCurrentJitter();

    // Even frame is unjittered (samples on pixel centres); odd frame is shifted by exactly ONE full-res pixel in x.
    CHECK(even.x == 0.0f && even.y == 0.0f);
    CHECK(std::fabs(odd.x - 1.0f / 3840.0f) < 1e-12f);
    CHECK(odd.y == 0.0f); // horizontal only: a diagonal shift covers just half the pixels
    CHECK(std::fabs(j.GetJitterDelta().x - (odd.x - even.x)) < 1e-12f);

    // JitterScale must NOT change the geometry (coverage needs exactly one pixel)
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 2.0f; });
    j.Update(1);
    CHECK(std::fabs(j.GetCurrentJitter().x - 1.0f / 3840.0f) < 1e-12f);
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 1.0f; });

    // JitterDirection flips the shift
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = -1; });
    j.Update(1);
    CHECK(std::fabs(j.GetCurrentJitter().x + 1.0f / 3840.0f) < 1e-12f);
    j.Update(0);
    CHECK(j.GetCurrentJitter().x == 0.0f);
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = 1; });
    j.Update(1);

    // Apply/Remove must be exact inverses
    float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0.25f, 0.5f, 1, 0,  0, 0, 0, 1 };
    float orig[16];
    std::copy(m, m + 16, orig);
    j.ApplyJitterToProjection(m, true);
    j.RemoveJitterFromProjection(m, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(m[i] - orig[i]) < 1e-7f);

    // SetProjectionJitter must be idempotent without compounding offsets
    float out1[16];
    float out2[16];
    float snapshot[16];
    std::copy(orig, orig + 16, snapshot);
    j.SetProjectionJitter(out1, orig, true);
    j.SetProjectionJitter(out2, orig, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(out1[i] - out2[i]) < 1e-7f);

    // ...and must produce exactly what Apply produces from the same unjittered matrix
    float viaApply[16];
    std::copy(orig, orig + 16, viaApply);
    j.ApplyJitterToProjection(viaApply, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(out1[i] - viaApply[i]) < 1e-7f);

    // Non-aliased output leaves the unjittered source untouched
    for (int i = 0; i < 16; ++i) CHECK(orig[i] == snapshot[i]);
}

// Geometric simulation of 2x MSAA checkerboard coverage, driven by the REAL JitterManager output.
// Independent of MapPixelToSample's closed form: positions come from the standard sample locations
// (sample 0 = (0.75,0.75), sample 1 = (0.25,0.25), y down) plus the jitter, and the pixel containing
// each sample position is found with floor().
static void TestCheckerboardGeometry() {
    constexpr int kW = 64, kH = 36; // target size (render target = 32 x 18)
    constexpr int kQW = kW / 2, kQH = kH / 2;

    for (int dir : { 1, -1 }) {
        ConfigManager::Get().Modify([dir](CBRConfig& c) { c.jitterDirection = dir; });
        JitterManager::Get().Initialize(kW, kH);

        int coverage[kH][kW] = {}; // how many frames natively shaded each pixel
        for (uint32_t parity = 0; parity < 2; ++parity) {
            JitterManager::Get().Update(parity);
            const double shiftPx = static_cast<double>(JitterManager::Get().GetCurrentJitter().x) * kW;
            for (int qy = 0; qy < kQH; ++qy) {
                for (int qx = 0; qx < kQW; ++qx) {
                    for (uint32_t s = 0; s < 2; ++s) {
                        const double px = 2.0 * (qx + kStandardSample2x[s].x) + shiftPx; // target-pixel units
                        const double py = 2.0 * (qy + kStandardSample2x[s].y);
                        const int tx = static_cast<int>(std::floor(px));
                        const int ty = static_cast<int>(std::floor(py));
                        // Every sample sits exactly on a pixel CENTRE (fractional part 0.5)
                        CHECK(std::fabs((px - std::floor(px)) - 0.5) < 1e-4);
                        CHECK(std::fabs((py - std::floor(py)) - 0.5) < 1e-4);
                        if (tx < 0 || tx >= kW || ty < 0 || ty >= kH) continue;
                        ++coverage[ty][tx];

                        const CheckerboardSample m = MapPixelToSample(tx, ty, parity, dir);
                        CHECK(m.active);
                        CHECK(m.quarterX == qx && m.quarterY == qy && m.sample == s);
                    }
                }
            }
        }
        // Over two frames every interior pixel is natively shaded exactly once (the checkerboard is complete)
        int wrong = 0;
        for (int y = 0; y < kH; ++y) {
            for (int x = 2; x < kW - 2; ++x) {
                if (coverage[y][x] != 1) ++wrong;
            }
        }
        CHECK(wrong == 0);

        // Parity bookkeeping: exactly half of all pixels are active in each frame
        for (uint32_t parity = 0; parity < 2; ++parity) {
            int active = 0;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    if (MapPixelToSample(x, y, parity, dir).active) ++active;
            CHECK(active == kW * kH / 2);
        }
    }
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = 1; });

    // Negative controls: the previous schemes must be rejected by the same simulation
    auto covered = [&](double sx0, double sy0, double sx1, double sy1) {
        bool seen[kH][kW] = {};
        const double shifts[2][2] = { { sx0, sy0 }, { sx1, sy1 } };
        for (const auto& sh : shifts)
            for (int qy = 0; qy < kQH; ++qy)
                for (int qx = 0; qx < kQW; ++qx)
                    for (const auto& so : kStandardSample2x) {
                        const int tx = static_cast<int>(std::floor(2.0 * (qx + so.x) + sh[0]));
                        const int ty = static_cast<int>(std::floor(2.0 * (qy + so.y) + sh[1]));
                        if (tx >= 0 && tx < kW && ty >= 0 && ty < kH) seen[ty][tx] = true;
                    }
        int n = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 2; x < kW - 2; ++x)
                n += seen[y][x] ? 1 : 0;
        return n;
    };
    const int interior = kH * (kW - 4);
    CHECK(covered(0, 0, 1, 0) == interior);                    // Intel scheme: complete
    CHECK(covered(-0.5, 0, 0.5, 0) == interior);              // symmetric +/-0.5: complete (but samples on pixel edges)
    CHECK(covered(-0.5, -0.5, 0.5, 0.5) == interior / 2);     // old diagonal scheme: only half the pixels, ever
}

static void TestDepthConvention() {
    // Round trip: project a view-space distance to device depth, linearize it back.
    for (double z : { 0.2, 1.0, 10.0, 100.0, 1000.0, 5000.0 }) {
        const double n = 0.1, f = 10000.0;
        const double dStd = f * (z - n) / (z * (f - n));  // standard: 0 near .. 1 far
        const double dRev = 1.0 - dStd;                   // reversed: 1 near .. 0 far
        const double dInf = n / z;                        // reversed, infinite far plane

        const float zStd = LinearizeDepth(static_cast<float>(dStd), DepthConvention::Standard, 0.1f, 10000.0f);
        const float zRev = LinearizeDepth(static_cast<float>(dRev), DepthConvention::Reversed, 0.1f, 10000.0f);
        const float zInf = LinearizeDepth(static_cast<float>(dInf), DepthConvention::Reversed, 0.1f, 0.0f);

        // float32 device depth limits precision (esp. standard-Z far away), so allow a tolerance
        CHECK(std::fabs(zRev - z) / z < 5e-3);
        CHECK(std::fabs(zInf - z) / z < 1e-4);
        if (z <= 100.0) CHECK(std::fabs(zStd - z) / z < 5e-3);
    }

    // Scale invariance: a 1% depth change gives the same relative delta at 1 m and at 1 km (reversed, infinite far)
    auto relDelta = [](double z) {
        const double n = 0.1;
        const float a = LinearizeDepth(static_cast<float>(n / z), DepthConvention::Reversed, 0.1f, 0.0f);
        const float b = LinearizeDepth(static_cast<float>(n / (z * 1.01)), DepthConvention::Reversed, 0.1f, 0.0f);
        return RelativeDepthDelta(a, b);
    };
    CHECK(std::fabs(relDelta(1.0) - relDelta(1000.0)) < 2e-3);
    CHECK(relDelta(1.0) > 0.008 && relDelta(1.0) < 0.012);

    // ...whereas an ABSOLUTE threshold on raw reversed-Z depth is not: the same 1% step differs by ~1000x
    const double absNear = std::fabs(0.1 / 1.0 - 0.1 / 1.01);
    const double absFar  = std::fabs(0.1 / 1000.0 - 0.1 / 1010.0);
    CHECK(absNear / absFar > 500.0);

    // Sanitisation: Standard needs a finite far plane
    const DepthRange r1 = SanitizeDepthRange(DepthConvention::Standard, 0.1f, 0.0f);
    CHECK(r1.zFar == 1000.0f && r1.zNear == 0.1f);
    const DepthRange r2 = SanitizeDepthRange(DepthConvention::Reversed, 0.1f, 0.0f);
    CHECK(r2.zFar == 0.0f); // infinite far is valid for Reversed
    const DepthRange r3 = SanitizeDepthRange(DepthConvention::Standard, 5.0f, 2.0f);
    CHECK(r3.zFar == 1000.0f); // far <= near is invalid
}

static void TestPushConstantBuilder() {
    auto& cfg = ConfigManager::Get().GetMutableConfig();
    cfg.depthTolerance = 0.02f;
    cfg.historyWeight = 0.8f;
    cfg.colorSpace = ColorSpace::RGB;
    cfg.enableSpatialFallback = false;
    cfg.enableMotionDilation = false;
    cfg.jitterCompensation = -1.0f;
    cfg.jitterScale = 1.0f;
    cfg.jitterDirection = -1;
    cfg.depthConvention = DepthConvention::Standard;
    cfg.depthNear = 0.5f;
    cfg.depthFar = 0.0f; // invalid for Standard -> must be sanitised

    RenderTargetManager::Get().Initialize(3840, 2160);
    JitterManager::Get().Initialize(3840, 2160);
    JitterManager::Get().Update(0);
    JitterManager::Get().Update(1);

    const ReconstructionPushConstants pc = BuildReconstructionPushConstants(7);
    CHECK(pc.targetResolution[0] == 3840.0f && pc.targetResolution[1] == 2160.0f);
    CHECK(std::fabs(pc.invTargetResolution[0] - 1.0f / 3840.0f) < 1e-12f);
    CHECK(pc.frameIndex == 7);
    CHECK(pc.depthTolerance == 0.02f && pc.historyWeight == 0.8f);
    CHECK(pc.colorSpace == 1u);
    CHECK(pc.enableSpatialFallback == 0u);
    CHECK(pc.enableMotionDilation == 0u);
    CHECK(pc.jitterCompensation == -1.0f);
    CHECK(pc.shiftDirection == -1);
    CHECK(pc.depthMode == 0u);
    CHECK(pc.depthNear == 0.5f && pc.depthFar == 1000.0f);
    CHECK(std::fabs(pc.jitterDelta[0] - JitterManager::Get().GetJitterDelta().x) < 1e-12f);

    cfg.enableSpatialFallback = true;
    cfg.enableMotionDilation = true;
    cfg.colorSpace = ColorSpace::YCoCg;
    cfg.jitterCompensation = 0.0f;
    cfg.jitterDirection = 1;
    cfg.depthConvention = DepthConvention::Reversed;
    const ReconstructionPushConstants pc2 = BuildReconstructionPushConstants(0);
    CHECK(pc2.enableSpatialFallback == 1u && pc2.enableMotionDilation == 1u && pc2.colorSpace == 0u);
    CHECK(pc2.shiftDirection == 1 && pc2.depthMode == 1u && pc2.depthFar == 0.0f);
}

static void TestRenderTargets() {
    auto& r = RenderTargetManager::Get();
    r.Initialize(3840, 2160);
    // 4K: quarter colour 33,177,600 + quarter depth 16,588,800 + history colour A/B 132,710,400
    //    + history depth A/B 66,355,200 + output 66,355,200 = 315,187,200 bytes (300.58 MiB, 315.19 MB)
    CHECK(r.GetTotalAllocatedVramBytes() == 315187200u);
    const double mib = static_cast<double>(r.GetTotalAllocatedVramBytes()) / (1024.0 * 1024.0);
    CHECK(std::fabs(mib - 300.58) < 0.01);

    // 1080p (the AMD Vega 7 profile): 78,796,800 bytes = 78.80 MB
    r.Initialize(1920, 1080);
    CHECK(r.GetTotalAllocatedVramBytes() == 78796800u);
    r.Initialize(3840, 2160);

    CHECK(r.IsTargetInterceptCandidate(3840, 2160, 0));
    CHECK(!r.IsTargetInterceptCandidate(1920, 1080, 0));

    CHECK(r.GetCurrentHistoryIndex() == 0 && r.GetPreviousHistoryIndex() == 1);
    r.SwapHistoryBuffers();
    CHECK(r.GetCurrentHistoryIndex() == 1 && r.GetPreviousHistoryIndex() == 0);
    r.ResetHistory();
    CHECK(r.GetCurrentHistoryIndex() == 0);
}

static void TestPushConstantLayout() {
    CHECK(sizeof(ReconstructionPushConstants) == 80);
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "cbr_tests";
    fs::create_directories(dir);

    TestConfigHardening(dir);
    TestConfigMissingFileAndRoundTrip(dir);
    TestLoggerBufferingAndLevel(dir);
    TestJitter();
    TestCheckerboardGeometry();
    TestDepthConvention();
    TestPushConstantBuilder();
    TestRenderTargets();
    TestPushConstantLayout();

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
```

<a id="teststestenginecpp"></a>
### `tests/test_engine.cpp`
```cpp
// Engine-level tests: hook-install retry, once-per-frame dispatch, frame-parity gating.
// The real hook installers are Windows-only, so this file supplies fakes for them.
//
//   cbr_engine_tests            -> PreferredApi = Vulkan
//   cbr_engine_tests auto       -> PreferredApi = Auto (no graphics runtime present on the host)
//
// The engine is a process-wide singleton with a one-shot Initialize(), hence one mode per process.
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/hooks.h"
#include "cbr/render_target_manager.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace cbr;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                        \
    } while (0)

// ---- Fake hook installers (replace hooks_vulkan.cpp / hooks_dx12.cpp for host tests) ----
static std::atomic<int> g_vulkanAttempts{ 0 };
static std::atomic<int> g_dx12Attempts{ 0 };
static std::atomic<int> g_vulkanFailuresRemaining{ 0 };

namespace cbr {
bool HookManager::InstallVulkanHooks() {
    ++g_vulkanAttempts;
    if (g_vulkanFailuresRemaining.load() > 0) {
        --g_vulkanFailuresRemaining;
        return false;
    }
    m_vulkanHooked.store(true);
    return true;
}
bool HookManager::InstallDX12Hooks() { ++g_dx12Attempts; m_dx12Hooked.store(true); return true; }
void HookManager::UninstallVulkanHooks() { m_vulkanHooked.store(false); }
void HookManager::UninstallDX12Hooks()   { m_dx12Hooked.store(false); }
} // namespace cbr

static int g_a = 0, g_b = 0; // distinct addresses used as fake present targets

static void RunVulkanTests() {
    auto& e = CBREngine::Get();
    auto& rt = RenderTargetManager::Get();

    CHECK(e.Initialize());
    CHECK(e.GetActiveApi() == GraphicsApi::Vulkan);
    CHECK(g_vulkanAttempts.load() == 0);                  // Initialize() must not install hooks itself

    // Hook install is retried until it succeeds, then never repeated
    g_vulkanFailuresRemaining = 3;
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 4);
    CHECK(e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 4);                  // already hooked: no further attempts
    CHECK(g_dx12Attempts.load() == 0);

    // Reconstruction + history swap happen at most once per frame
    CHECK(e.GetCurrentFrameIndex() == 0);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 1);
    // One duplicate call: an unguarded engine would swap twice and land back on index 0. (An odd
    // number of duplicates would pass by parity luck, so keep the total number of calls even.)
    e.OnScenePassEnd(nullptr);                            // second matching pass in the same frame
    CHECK(rt.GetCurrentHistoryIndex() == 1);

    // Parity only advances for the main present target
    e.OnPostPresent(&g_a);                                // first present defines the main target
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_b);                                // overlay / secondary swapchain
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_a);
    CHECK(e.GetCurrentFrameIndex() == 2);

    // New frame -> one more swap allowed
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);

    // Disabled engine does nothing
    e.SetEnabled(false);
    e.OnPostPresent(&g_a);                                // frame advances, but...
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);              // ...no swap while disabled
    e.SetEnabled(true);

    // Swapchain recreation resets parity, history and the once-per-frame guard
    e.OnSwapchainRecreated();
    CHECK(e.GetCurrentFrameIndex() == 0);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 1);              // frame 0 is dispatchable again
    e.OnPostPresent(&g_b);                                // a different target may become the main one
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_a);
    CHECK(e.GetCurrentFrameIndex() == 1);

    // Swapchain extents are untrusted: odd sizes round up to even, implausible ones are ignored
    e.OnSwapchainRecreated(1921, 1081);
    CHECK(rt.GetDimensions().fullWidth == 1922 && rt.GetDimensions().fullHeight == 1082);
    CHECK(rt.GetDimensions().quarterWidth == 961 && rt.GetDimensions().quarterHeight == 541);

    e.OnSwapchainRecreated(0, 0); // minimised window
    CHECK(rt.GetDimensions().fullWidth == 1922);

    e.OnSwapchainRecreated(5, 5); // too small
    CHECK(rt.GetDimensions().fullWidth == 1922);

    e.OnSwapchainRecreated(100000, 100000); // absurd
    CHECK(rt.GetDimensions().fullWidth == 1922 && rt.GetDimensions().fullHeight == 1082);

    e.OnSwapchainRecreated(3840, 2160);
    CHECK(rt.GetDimensions().fullWidth == 3840 && rt.GetDimensions().fullHeight == 2160);
}

static void RunAutoTests() {
    auto& e = CBREngine::Get();
    CHECK(e.Initialize());
    // No graphics runtime is loaded on the host: Auto must stay pending and attempt NO installs
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 0);
    CHECK(g_dx12Attempts.load() == 0);
}

int main(int argc, char** argv) {
    const bool autoMode = (argc > 1 && std::strcmp(argv[1], "auto") == 0);

    const fs::path dir = fs::temp_directory_path() / (autoMode ? "cbr_engine_tests_auto" : "cbr_engine_tests");
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "cbr.ini");
        f << "[General]\nEnabled = true\nTargetWidth = 3840\nTargetHeight = 2160\n"
          << "PreferredApi = " << (autoMode ? "Auto" : "Vulkan") << "\n[Debug]\nLogToFile = false\n";
    }
    CBREngine::Get().SetModuleDirectory(dir);

    if (autoMode) RunAutoTests(); else RunVulkanTests();

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << (autoMode ? "Engine (auto) tests passed.\n" : "Engine tests passed.\n");
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
```

<a id="githubworkflowsbuildyml"></a>
### `.github/workflows/build.yml`
```yaml
# NOTE: written but not executed by the author's tooling; verify on first push.
name: build

on:
  push:
  pull_request:

jobs:
  tests-and-shaders:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install tools
        run: sudo apt-get update && sudo apt-get install -y cmake g++ glslang-tools
      - name: Unit tests
        run: |
          cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
          cmake --build build-tests --target cbr_tests cbr_engine_tests
          ctest --test-dir build-tests --output-on-failure
      - name: Validate shaders
        run: |
          glslangValidator -V shaders/cbr_reconstruct.comp -o /tmp/r.spv
          glslangValidator -V shaders/cbr_resolve_simple.comp -o /tmp/s.spv
          glslangValidator -D -e CSMain -S comp -V shaders/cbr_reconstruct.hlsl -o /tmp/h.spv

  windows-release:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -A x64
      - name: Build (Release, MSVC hardening flags)
        run: cmake --build build --config Release
      - name: SHA-256 of release artifacts
        shell: pwsh
        run: |
          Get-FileHash build/bin/Release/rdr2-cbr.asi -Algorithm SHA256 |
            ForEach-Object { "$($_.Hash)  rdr2-cbr.asi" } | Tee-Object build/bin/Release/SHA256SUMS.txt
      - uses: actions/upload-artifact@v4
        with:
          name: rdr2-cbr-release
          path: |
            build/bin/Release/rdr2-cbr.asi
            build/bin/Release/cbr.ini
            build/bin/Release/SHA256SUMS.txt
```
