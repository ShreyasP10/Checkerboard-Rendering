# RDR2 Checkerboard Rendering Mod (CBR)

<p align="center">
  <strong>PlayStation 4 Pro-style 2× MSAA temporal checkerboard reconstruction for <em>Red Dead Redemption 2</em> (PC)</strong>
</p>

<p align="center">
  <a href="https://github.com/ShreyasP10/Checkerboard-Rendering/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/ShreyasP10/Checkerboard-Rendering/build.yml?branch=feature%2Fcbr-pipeline&style=flat-square&label=ci" alt="CI status"></a>
  <a href="https://github.com/ShreyasP10/Checkerboard-Rendering/releases"><img src="https://img.shields.io/github/v/release/ShreyasP10/Checkerboard-Rendering?style=flat-square&color=blue&label=release" alt="Latest Release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64-blue?style=flat-square" alt="Platform: Windows x64">
  <img src="https://img.shields.io/badge/API-Vulkan%20%7C%20DX12-green?style=flat-square" alt="API: Vulkan | DX12">
  <img src="https://img.shields.io/badge/target-GTX%201070%20Ti%20%7C%20Vega%207-orange?style=flat-square" alt="Target: GTX 1070 Ti | Vega 7">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square" alt="License: MIT"></a>
</p>

A community-driven native ASI graphics modification implementing **Checkerboard Rendering (CBR)** in *Red Dead Redemption 2* (PC), bringing PlayStation 4 Pro-style half-rate 2× MSAA temporal reconstruction to PC GPUs—specifically targeting the **NVIDIA GeForce GTX 1070 Ti (Pascal)** and **AMD Radeon Vega 7 (GCN 5.0 APU)** without Tensor cores.

**Offline, with no catch.** Configuration (`cbr.ini`), diagnostic logs (`cbr.log`), crash telemetry, and compiled SPIR-V shaders live strictly inside your local RDR2 folder. The ASI binary is compiled with a static CRT (zero external Visual C++ Redistributables required), makes zero network requests, and enforces strict single-player safety via `CheckAntiCheatModules()` so CBR remains inert outside offline Story Mode. Architecture specs, shader equivalence proofs, and in-game telemetry reports live in the [Documentation](#documentation).

<p align="center">
  <a href="#download-releases">Releases</a> ·
  <a href="#install">Install</a> ·
  <a href="#why-checkerboard-rendering-cbr-vs-dlss--fsr">Why CBR</a> ·
  <a href="#how-it-works">How It Works</a> ·
  <a href="#documentation">Docs</a> ·
  <a href="#configuration-cbrini">Configuration</a> ·
  <a href="#testing">Testing</a> ·
  <a href="#changelog">Changelog</a> ·
  <a href="#contributing">Contributing</a>
</p>

> **🚀 Project Status: Active Graphics Interception & Pipeline Execution Phase**  
> The mod runs active **MinHook** detours across the **Vulkan** pipeline (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkAcquireNextImageKHR`, `vkAcquireNextImage2KHR`, `vkCreateFramebuffer`, `vkCreateImageView`, `vkCreateRenderPass`/`2`, `vkEnumeratePhysicalDevices`, `vkCreateSampler`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, `vkCmdSetSampleLocationsEXT`), capturing rendered geometry into quarter-res 2× MSAA buffers, managing ping-pong history descriptor sets (`historyColorViewA`/`B`) + `binding = 7` camera UBO with pre-dispatch layout barriers, executing `cbr_reconstruct.spv` mid-frame on `vkCmdEndRenderPass`, and blitting the reconstructed output into the intermediate full-res scene color target without overwriting UI/HUD overlays. It also includes mutual-exclusive Strategy A / Strategy B sampling, selective `-1.0` `MipLodBias` on anisotropic mipmapped samplers, multi-region `VirtualQuery` AVX2 AOB scanning, optional camera-only depth unprojection fallback & 5-tap RCAS sharpening (`SharpeningStrength = 0.0` default), online CLI argument safety (`-rdo`, `-scOnlineOnly`, `-Multiplayer`), loader-lock-safe preloaded `dbghelp.dll` minidumps, and `F11` / `Insert` telemetry OSD controls.  
> *Note on parity:* DirectX 12 (`hooks_dx12.cpp`, `render_target_manager.cpp`, `reconstruction_pass.cpp`) implements DXGI swapchain + `ID3D12CommandQueue`/`ID3D12GraphicsCommandList` VMT detours, committed resources, descriptor heaps, root signature, compute PSO, and `DispatchDX12` barrier submission, while live RDR2 DX12 scene attachment capture & visual Dear ImGui swapchain rendering remain pending in-game validation; Vulkan (`PreferredApi = Vulkan`) is the primary active backend. Pre-built binaries are available on [GitHub Releases](https://github.com/ShreyasP10/Checkerboard-Rendering/releases).

---

<a id="download-releases"></a>
## 📥 Download & Releases

Pre-built binaries and complete packages are published via CI on [GitHub Releases](https://github.com/ShreyasP10/Checkerboard-Rendering/releases):

| Package / Asset | Description | Direct Download |
|---|---|---|
| **`rdr2-cbr-v0.1.0-alpha.zip`** | **Complete Package** (`rdr2-cbr.asi`, `cbr.ini`, `shaders/`, `README_INSTALL.txt`) | [⬇️ **Download ZIP**](https://github.com/ShreyasP10/Checkerboard-Rendering/releases/download/v0.1.0-alpha/rdr2-cbr-v0.1.0-alpha.zip) |
| **`rdr2-cbr.asi`** | Native Windows x64 ASI Mod Plugin binary | [⬇️ **Download .asi**](https://github.com/ShreyasP10/Checkerboard-Rendering/releases/download/v0.1.0-alpha/rdr2-cbr.asi) |
| **`cbr.ini`** | Default runtime configuration file | [⬇️ **Download .ini**](https://github.com/ShreyasP10/Checkerboard-Rendering/releases/download/v0.1.0-alpha/cbr.ini) |
| **Release Page** | Changelogs, checksums (`SHA256SUMS.txt`), and all tagged builds | [🔗 **View GitHub Releases**](https://github.com/ShreyasP10/Checkerboard-Rendering/releases) |

<a id="quick-start"></a>
### ⚡ Quick 3-Step Setup
1. **Download:** Grab [`rdr2-cbr-v0.1.0-alpha.zip`](https://github.com/ShreyasP10/Checkerboard-Rendering/releases/download/v0.1.0-alpha/rdr2-cbr-v0.1.0-alpha.zip) from the table above (and verify against `SHA256SUMS.txt`).
2. **Install ASI Loader:** Place `dinput8.dll` from the official [Script Hook RDR2](https://www.dev-c.com/rdr2/scripthookrdr2/) release into your main RDR2 directory (where `RDR2.exe` lives).
3. **Deploy & Launch:** Extract `rdr2-cbr.asi`, `cbr.ini`, and the `shaders/` directory next to `RDR2.exe`, confirm `Graphics API = Vulkan` in RDR2 settings, and launch **Story Mode**.

---

## 📖 Table of Contents
- [Download & Releases](#download-releases)
- [About](#about)
- [Why Checkerboard Rendering? (CBR vs. DLSS / FSR)](#why-checkerboard-rendering-cbr-vs-dlss--fsr)
- [How It Works](#how-it-works)
- [Key Features](#key-features)
- [Architecture & Repository Structure](#architecture--repository-structure)
- [Engineering Documentation](#documentation)
- [Reconstruction Shader Math](#reconstruction-shader-math)
- [Hardware & Development Requirements](#hardware--development-requirements)
- [How to Install & Run](#install)
- [Configuration (`cbr.ini`)](#configuration-cbrini)
- [Testing](#testing)
- [Changelog](#changelog)
- [Contributing](#contributing)
- [Collaborators & Maintainers](#collaborators--maintainers)
- [License](#license)
- [Disclaimer](#disclaimer)

---

## About

On the PlayStation 4 Pro, *Red Dead Redemption 2* targets 4K output by rendering half the shaded pixels per frame (quarter-resolution targets configured with **2× MSAA**) in an alternating checkerboard lattice and reconstructing the missing subpixels over time.

On PC, players on mid-range discrete GPUs (such as the **NVIDIA GeForce GTX 1070 Ti**) or integrated APUs (**AMD Radeon Vega 7**) face a clear gap:
- **Native 4K (3840×2160)** saturates fill-rate and memory bandwidth well short of 60 FPS.
- **DLSS** requires RTX Tensor cores and cannot run on GTX 10-series Pascal or Vega APUs.
- **FSR** upscales a conventional flat 2D render target and carries its own temporal sharpening/edge signature rather than hardware MSAA checkerboard geometry.

This project provides a **native ASI plugin** (`rdr2-cbr.asi`) that intercepts RDR2's primary scene render pass, captures quarter-resolution 2× MSAA color/depth targets with a 2-frame alternating subpixel pattern, reconstructs full resolution via compute shaders with depth disocclusion testing and neighborhood variance clipping (`YCoCg`, `RGB`, or HDR10 `ICtCp`), and restores native texture clarity with a **-1.0 MIP LOD bias** before post-processing and UI composition.

---

## Why Checkerboard Rendering? (CBR vs. DLSS / FSR)

**Inspiration: The PlayStation 4 Pro**  
By shading half the pixels per frame on an offset 2× MSAA grid and mathematically filling the complement from historical frames, CBR preserves real geometric edge detail at half the fragment-shading cost. This mod brings that architecture to PC Vulkan/DX12 hardware without proprietary ML cores.

**How is CBR different from DLSS or FSR?**  
Conventional spatial/temporal upscalers operate on uniform, non-MSAA low-resolution buffers:
* **DLSS / XeSS (XMX):** Depend on specialized AI matrix/Tensor hardware and **cannot run on Pascal GTX 10-series or Vega APUs**.
* **FSR 2.0+:** Runs across GPUs, but reconstructs from a flat rectangular grid, which can soften thin geometry or accentuate sharpening halos.

**What extra benefits does CBR provide?**
1. **Geometric Fidelity (2× MSAA):** Because odd/even frames stagger 2× MSAA sample centroids across complementary subpixel quadrants, two frames physically sample **100% of the 4K (or 1080p) geometric grid**, retaining crisp silhouette and foliage edges.
2. **Universal Hardware Efficiency:** Pure compute math—compatible with NVIDIA Pascal (Wave32, 16×16 or 8×8 quad workgroups) and AMD GCN 5.0 Vega APUs (Wave64 alignment) with zero AI hardware dependencies.
3. **Authentic Console Temporal Look:** Preserves the temporal response and texture crispness (`MipLodBias = -1.0`) of PS4 Pro-style reconstruction on PC.

---

## How It Works

```text
Frame N:     [ X ] [   ] [ X ] [   ]  <-- Shaded at Quarter-Res 2× MSAA + Strategy A/B Jitter
             [   ] [ X ] [   ] [ X ]      (Scene render pass intercepted; UI/HUD stays full-res)
               |
               v
Reconstruct: Current Active Pixels  <─── Unpack exact MSAA Sample 0 / Sample 1
             Missing Holes          <─── Reproject History Buffer (N-1) via Motion Vectors
                                         [Depth Linearization, Clamping & Spatial Fallback]
               |
               v
Output:      Full 3840×2160 Frame   ───> Blitted back to Game Scene HDR Attachment pre-UI
```

1. **Target Interception & Dynamic Redirection:** Intercepts `vkCreateRenderPass`/`2`, `vkCreateFramebuffer`, `vkCreateImageView`, `vkAcquireNextImageKHR`/`2KHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, and `vkCmdBeginRenderPass` (gated by attachment inspection + `draws >= 250`) to redirect full-resolution geometry passes to quarter-resolution (e.g., 1920×1080 for 4K, 960×540 for 1080p) 2× MSAA targets while keeping HUD/UI passes at native output resolution.
2. **Mutual-Exclusive Subpixel Sampling (`StrategyA` vs. `StrategyB`):**
   - **`StrategyA` (Viewport Shift / Projection Jitter):** Shifts the viewport / camera projection horizontally by a complementary subpixel offset on odd frames, scanned dynamically across readable committed regions of `RDR2.exe` via the AVX2 SIMD AOB scanner (`pattern_scanner.cpp`) with non-accumulating tracked-base / previous-offset jitter handling.
   - **`StrategyB` (`VK_EXT_sample_locations`):** Alternates hardware 2× MSAA programmable sample coordinates (`(0.75, 0.25) / (0.25, 0.75)` on even frames, mirrored on odd frames) while keeping the viewport stationary.
3. **Selective MIP LOD Bias (`-1.0`):** Applies the default `-1.0` `MipLodBias` selectively inside `Hooked_vkCreateSampler` ONLY to mipmapped anisotropic scene samplers (`maxLod > 1.0f && anisotropyEnable == VK_TRUE`), compensating for 2× wider texture gradients at quarter resolution without distorting UI/LUT samplers.
4. **Mid-Frame Compute Reconstruction & Scene Blit:** At `vkCmdEndRenderPass` (prior to UI/HUD compositing), copies scene attachments into quarter-res buffers, transitions all 7 ping-pong images safely out of `UNDEFINED`, binds the alternating `VkDescriptorSet` + `binding = 7` camera UBO, runs `cbr_reconstruct.spv` (or quad-per-thread `cbr_reconstruct_quad.comp`, which evaluates both active 2×2 quad pixels first and shares their samples in thread-local registers without requiring `GL_KHR_shader_subgroup_quad` cross-lane intrinsics, or HLSL `cbr_reconstruct.hlsl`) with linearized depth disocclusion, optional camera-only depth unprojection fallback, NaN/Inf sanitization, `YCoCg`/`RGB`/`ICtCp` variance clipping (`gamma = 1.25`), cross-bilateral fallback, and optional 5-tap RCAS sharpening (`SharpeningStrength`, default `0.0`), then blits the reconstructed frame into the game's intermediate full-resolution scene color attachment (falling back to pre-present blit only if no intermediate scene target is tracked).

---

## Key Features

- [x] **Engineering Specifications & Hardware Guides:** Complete PRD, SRD, SRS, TRD, DEV_PLAN, RISK_REGISTER, Intel 2018 CBR reference analysis, AMD Vega 7 optimization guide, Vulkan hooking guide, and live test report (`docs/IN_GAME_TEST_REPORT_v1491.md`).
- [x] **Multi-Backend Reconstruction Shaders:** GLSL (`cbr_reconstruct.comp`), 2×2 quad-per-thread GLSL (`cbr_reconstruct_quad.comp`, sharing active samples in thread-local registers without requiring subgroup extensions), spatial baseline fallback (`cbr_resolve_simple.comp`), and DirectX 12 HLSL (`cbr_reconstruct.hlsl`) verified for 100% coordinate-mapping equivalence across 36,864 combinations.
- [x] **Temporal Reprojection, Velocity Fallback & 5-Tap RCAS:** Reads motion vectors with automatic quarter-resolution coordinate downshifting (`>> 1` clamp), optional 3×3 closest-depth motion dilation, camera-only depth unprojection fallback (`u_CurrViewProjInv` / `u_PrevViewProj` via `binding = 7` / `register(b1)`), NaN/Inf sanitization on active/fallback paths, full-resolution history-depth output (`u_OutputDepth` / `g_OutputDepth`), and optional fused 5-tap RCAS sharpening (`SharpeningStrength`, default `0.0`).
- [x] **Depth Convention & Disocclusion Defense:** Supports both RDR2 Reversed-Z (`DepthFar = 0.0` infinite far plane) and Standard linear depth unprojection (`depth_convention.h`) with cardinal cross-bilateral fallback on disocclusion.
- [x] **Three Color-Space Clamping Modes (`gamma = 1.25`):** Unconditional 3×3 neighborhood variance clipping selectable between **YCoCg** (`0`), **RGB** (`1`), and **HDR10 / BT.2100 ICtCp** (`2`) to eliminate ghosting and pattern shimmer on static and dynamic scenes.
- [x] **Live Vulkan MinHook Pipeline:** Active scoped detours on `vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkAcquireNextImageKHR`, `vkAcquireNextImage2KHR`, `vkCreateRenderPass`/`2`, `vkEnumeratePhysicalDevices`, `vkCreateSampler` (filtered anisotropic LOD bias), `vkCreateFramebuffer`, `vkCreateImageView`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, and `vkCmdSetSampleLocationsEXT` with ping-pong descriptor sets, GPU `VkQueryPool` timing, and intermediate scene color composite.
- [x] **DirectX 12 Runtime Parity Plumbing (`hooks_dx12.cpp`, `render_target_manager.cpp`, `reconstruction_pass.cpp`):** DXGI + `ID3D12CommandQueue::ExecuteCommandLists` and `ID3D12GraphicsCommandList` VMT hooks, `CreateCommittedResource` + `CBV_SRV_UAV` / `SAMPLER` descriptor heaps, root signature + PSO compilation, and `DispatchDX12` barrier/compute submission.
- [x] **AVX2 SIMD Camera AOB Scanner:** Runtime CPUID AVX2 detection (`_mm256_cmpeq_epi8`) with scalar fallback, IDA wildcard parsing, RIP-relative resolution, SEH bounds protection, and region-by-region `VirtualQuery` traversal across `.text` sub-regions.
- [x] **Interactive HUD API & Telemetry (`F11` / `Insert`):** Foreground-PID-gated hotkey toggling, live parameter setters (`SetHistoryWeight`, `SetDepthTolerance`, `SetMipLodBias`, `SetDebugView`, `SetColorSpace`, etc.), built-in default `DefaultTelemetryHudCallback` logging/debug OSD output (`GetFormattedTelemetry()`, FPS, and reconstruction duration), and pluggable `HudRenderCallback`.
- [x] **Multi-Mode Debug Visualizer (`DebugView = 0..5`):** Reconstructed Output (`0`), Checkerboard Parity Mask (`1`), Disocclusion Heatmap (`2`), Motion Vector Field (`3`), Quarter-Res Raw (`4`), and Spatial Baseline (`5`).
- [x] **Hardened Runtime, Safety Gates & CI:** Bounded `cbr.ini` parser, online launch-argument anti-cheat gate (`-rdo`, `-scOnlineOnly`, `-Multiplayer`), loader-lock-safe preloaded `dbghelp.dll` minidump `CrashHandler`, centralized limits (`include/cbr/limits.h`), CTest C++ unit suites (`cbr_tests`, `cbr_engine_tests`), headless Vulkan Lavapipe compute test (`cbr_vulkan_headless`), and automated GitHub Actions release builds.
- [ ] **Live In-Game RDR2 DX12 Attachment Capture & Visual ImGui Draw Backend:** D3D12 VMT hooks and compute dispatch plumbing are implemented cleanly in source, while live in-game RAGE DX12 resource-binding validation, full visual Dear ImGui swapchain draw rendering, and multi-patch ViewProj AOB signatures remain open for live game testing.

---

## Architecture & Repository Structure

```text
Checkerboard-Rendering/
├── CMakeLists.txt                   # CMake build script for rdr2-cbr.asi & test targets
├── cbr.ini                          # Default runtime configuration
├── CHECKERBOARD.md                  # Foundational checkerboard rendering paper notes & math
├── CODEBASE.md                      # Single-file synchronized codebase reference snapshot
├── CONTRIBUTING.md                  # Contribution guidelines & C++20/GLSL/HLSL standards
├── LICENSE                          # MIT License
├── nextImplementationPhase.md       # Master roadmap for Phase 2 runtime interception
├── readme.md                        # Project overview & setup guide
├── REPORT.md                        # Final architectural engineering & verification report
├── .github/workflows/build.yml      # CI: unit tests, glslang/dxc shaders, MinGW syntax, Release ZIP
│
├── docs/                            # Engineering specifications, audits & hardware guides
│   ├── PRD.md                       # Product Requirements Document
│   ├── SRD.md                       # System Requirements Document
│   ├── SRS.md                       # Software Requirements Specification (IEEE 830)
│   ├── TRD.md                       # Technical Requirements Document
│   ├── DEV_PLAN.md                  # Phased Development Roadmap & Milestones
│   ├── RISK_REGISTER.md             # Risk Analysis & Mitigation Matrix
│   ├── INTEL_CBR_REFERENCE.md       # Intel 2018 2× MSAA / SRT / depth disocclusion breakdown
│   ├── AMD_VEGA_OPTIMIZATION.md     # AMD Radeon Vega 7 (GCN 5.0 / Wave64) tuning profile
│   ├── VULKAN_HOOKING_GUIDE.md      # Vulkan detour mechanics, barriers & compute binding
│   ├── IN_GAME_TEST_REPORT_v1491.md # Live RDR2 v1.0.1491.50 load & VRAM test report
│   ├── IMPROVEMENTS.md              # Verified implementation changelog & prioritized backlog
│   ├── REVIEW.md / REVIEW_v4.md     # Architecture, threading & shader audit reviews
│   └── nextImplementationPhase.md   # Phase 2 detour implementation blueprint
│
├── include/cbr/                     # Public & internal C++20 architecture headers (13 files)
│   ├── cbr_engine.h                 # Core engine lifecycle, API selection & frame orchestration
│   ├── checkerboard_mapping.h       # Subpixel parity & 2× MSAA sample coordinate mapping
│   ├── config.h                     # Thread-safe cbr.ini parser, enums & ColorSpace::ICtCp
│   ├── crash_handler.h              # Windows SEH exception interceptor & minidump telemetry
│   ├── depth_convention.h           # Standard vs Reversed-Z depth linearization helpers
│   ├── hooks.h                      # Vulkan & DX12 hook manager interface
│   ├── jitter_manager.h             # Non-accumulating Strategy A/B projection & sample jitter
│   ├── limits.h                     # Canonical resolution, file & clipping constants (gamma=1.25)
│   ├── logger.h                     # Async background worker cbr.log logger
│   ├── pattern_scanner.h            # AVX2 SIMD + scalar AOB signature scanner
│   ├── reconstruction_pass.h        # Compute pipeline, descriptors, barriers & push constants
│   ├── render_target_manager.h      # Quarter-res 2× MSAA & ping-pong history buffer allocation
│   └── ui_overlay.h                 # Interactive F11/Insert HUD controls & telemetry OSD API
│
├── src/                             # C++20 implementation modules (13 files)
│   ├── main.cpp                     # DllMain (pinned module), CBRInitThread & CBR_PluginInit
│   ├── cbr_engine.cpp               # Anti-cheat/CLI gate, hook retry, push constants & parity
│   ├── config.cpp                   # Bounded INI parser/serializer with validation
│   ├── crash_handler.cpp            # Loader-lock-safe preloaded dbghelp.dll minidump telemetry
│   ├── hooks.cpp                    # HookManager lifecycle & API routing
│   ├── hooks_vulkan.cpp             # Scoped Vulkan detours, attachment copy & mid-frame compute trigger
│   ├── hooks_dx12.cpp               # DXGI + ID3D12CommandQueue/GraphicsCommandList VMT detours
│   ├── jitter_manager.cpp           # Non-accumulating Strategy A/B projection & sample jitter
│   ├── logger.cpp                   # Bounded lock-free/queued asynchronous file writer
│   ├── pattern_scanner.cpp          # AVX2 + scalar IDA pattern scanner (multi-region VirtualQuery)
│   ├── reconstruction_pass.cpp      # Vulkan + D3D12 compute pipeline, UBO/CBV & dispatch
│   ├── render_target_manager.cpp    # VRAM footprint math & VkImage / ID3D12Resource allocation
│   └── ui_overlay.cpp               # Hotkey gating, live tuning setters & DefaultTelemetryHudCallback
│
├── shaders/                         # GPU compute shaders (4 files)
│   ├── cbr_reconstruct.comp         # Vulkan GLSL compute shader (16×16 Wave32/Wave64)
│   ├── cbr_reconstruct_quad.comp    # Quad-per-thread GLSL shader (8×8, register-shared active samples)
│   ├── cbr_reconstruct.hlsl         # DirectX 12 HLSL compute shader (SM 5.1 / SM 6.0)
│   └── cbr_resolve_simple.comp      # Spatial-only fallback resolve shader
│
└── tests/                           # Portable unit, shader & headless GPU compute tests
    ├── test_core.cpp                # Config, logger, jitter, push constants, camera UBO, depth & scanner tests
    ├── test_engine.cpp              # Hook retry, once-per-frame guard, CLI safety gate, parity & HUD tests
    ├── test_vulkan_headless.cpp     # Headless Vulkan compute dispatch & Frame 0/1 readback test (Lavapipe CI)
    └── check_shader_mapping.py      # 36,864-combination proof of 4-quadrant MSAA coverage
```

---

<a id="documentation"></a>
## 📚 Engineering Documentation

Every architectural decision, hardware profile, and audit finding is documented and synchronized in [`CODEBASE.md`](CODEBASE.md):

| Area / Goal | Document |
|---|---|
| Architectural overview & verification summary | [`REPORT.md`](REPORT.md) |
| Foundational CBR theory, 2× MSAA grids & `-1.0` LOD bias | [`CHECKERBOARD.md`](CHECKERBOARD.md) |
| Synchronized single-file codebase snapshot | [`CODEBASE.md`](CODEBASE.md) |
| Phase 2 runtime detours & swapchain integration plan | [`nextImplementationPhase.md`](nextImplementationPhase.md) · [`docs/nextImplementationPhase.md`](docs/nextImplementationPhase.md) |
| Live test report on RDR2 `1.0.1491.50` (GTX 1070 Ti + LML) | [`docs/IN_GAME_TEST_REPORT_v1491.md`](docs/IN_GAME_TEST_REPORT_v1491.md) |
| Vulkan loader detours, image barriers & compute pass guide | [`docs/VULKAN_HOOKING_GUIDE.md`](docs/VULKAN_HOOKING_GUIDE.md) |
| Intel 2018 CBR reference analysis (Mcferron & Lake) | [`docs/INTEL_CBR_REFERENCE.md`](docs/INTEL_CBR_REFERENCE.md) |
| AMD Radeon Vega 7 APU profile (GCN 5.0, Wave64, DDR4) | [`docs/AMD_VEGA_OPTIMIZATION.md`](docs/AMD_VEGA_OPTIMIZATION.md) |
| Product & system architecture requirements | [`docs/PRD.md`](docs/PRD.md) · [`docs/SRD.md`](docs/SRD.md) |
| IEEE 830 Software & Technical Requirements (SRS / TRD) | [`docs/SRS.md`](docs/SRS.md) · [`docs/TRD.md`](docs/TRD.md) |
| Development roadmap & risk register | [`docs/DEV_PLAN.md`](docs/DEV_PLAN.md) · [`docs/RISK_REGISTER.md`](docs/RISK_REGISTER.md) |
| Verified implementation changelog & code review audits | [`docs/IMPROVEMENTS.md`](docs/IMPROVEMENTS.md) · [`docs/REVIEW.md`](docs/REVIEW.md) · [`docs/REVIEW_v4.md`](docs/REVIEW_v4.md) |

---

## Reconstruction Shader Math

The core compute shader resolves each output pixel based on checkerboard parity:

$$\text{Phase}(x, y) = (x + y) \pmod 2$$

- When $\text{Phase}(x, y) = (\text{FrameIndex} \pmod 2)$, the pixel reads directly from the current frame's 2× MSAA sample mapping (`include/cbr/checkerboard_mapping.h`).
- When $\text{Phase}(x, y) \neq (\text{FrameIndex} \pmod 2)$, the pixel reprojects history using closest-depth dilated velocity vectors:

$$\mathbf{UV}_{\text{prev}} = \mathbf{UV}_{\text{curr}} - \mathbf{V}(x, y) - (\text{JitterCompensation} \cdot \Delta \mathbf{J})$$

Depth values are converted to view-space linear depth (`depth_convention.h`, supporting RDR2 **Reversed-Z** with infinite far plane and **Standard** depth) and tested for disocclusion:

$$\Delta Z = \frac{|Z_{\text{curr}} - Z_{\text{prev}}|}{\max(Z_{\text{curr}}, 10^{-5})} > \text{DepthTolerance}$$

Valid history samples undergo unconditional 3×3 neighborhood variance clipping (`kVarianceClipGamma = 1.25` in YCoCg, RGB, or HDR10 ICtCp space) before temporal blending (`HistoryWeight = 0.90`), followed optionally by a 5-tap RCAS contrast-adaptive sharpening pass (`SharpeningStrength`, default `0.0`). When velocity is zeroed/unbound and camera matrices are supplied (`binding = 7` / `register(b1)`), a camera-only depth unprojection fallback computes rigid motion via `u_CurrViewProjInv` and `u_PrevViewProj`. When disoccluded (or when non-finite NaN/Inf inputs are detected), the shader rejects history and blends a spatial cross-bilateral filter over the four cardinal active samples:

$$C_{\text{spatial}} = \frac{\sum_{k=1}^4 w_k C_k}{\sum_{k=1}^4 w_k}, \quad w_k = \exp\left(-\frac{\|p_k - p\|^2}{2\sigma_d^2}\right) \cdot \exp\left(-\frac{|Z_k - Z|^2}{2\sigma_z^2}\right)$$

---

## Hardware & Development Requirements

| Component | Minimum Specification | Recommended (Target Discrete) | Recommended (Target APU / Integrated) | Role in CBR Pipeline |
|---|---|---|---|---|
| **GPU** | GTX 1060 (6 GB) / RX 580 (8 GB) / Vega 7 (Vulkan 1.3 / DX12) | **NVIDIA GeForce GTX 1070 Ti (8 GB GDDR5)** | **AMD Radeon Vega 7 (Ryzen 5 4600G/5600H/5700U APU)** | Wave32 (Pascal) / Wave64 (Vega) compute shader execution, 2× MSAA rasterization |
| **GPU VRAM** | 1 GB (1080p CBR) / 6 GB (4K CBR) | **8 GB GDDR5** (`300.58 MiB` / `315.19 MB` CBR buffers at 4K) | **512 MB – 2 GB Shared UMA DDR4** (`75.15 MiB` / `78.80 MB` CBR buffers at 1080p) | Stores quarter-res 2× MSAA, ping-pong history, and output targets |
| **CPU** | Quad-Core (i5-8400 / Ryzen 2600; scalar AOB fallback) | **6-Core / 12-Thread (AVX2 capable)** | **AMD Ryzen 5 4600G / 5600H (6C / 12T, AVX2)** | Frame pacing, intercept dispatch & AVX2 SIMD AOB scanning |
| **RAM** | 8 GB Dual-Channel | **16 GB DDR4 Dual-Channel** | **16 GB Dual-Channel DDR4-3200+** | Essential on APUs for shared GPU/CPU memory bandwidth |
| **OS** | Windows 10 (64-bit, 19041+) | **Windows 10 / Windows 11 (64-bit)** | **Windows 10 / Windows 11 (64-bit)** | Native `vulkan-1.dll` (1.3) and DirectX 12 runtime |
| **Target Presentation** | 1080p (reconstructed from 540p 2× MSAA) | **Native 4K (3840×2160)** reconstructed from 1080p 2× MSAA | **Native 1080p (1920×1080)** reconstructed from 540p 2× MSAA | Output presentation resolution |

---

<a id="install"></a>
<a id="building"></a>
## 🚀 How to Install & Run

### Option A: Download Pre-built Binaries (Recommended)
1. Open the [Releases](https://github.com/ShreyasP10/Checkerboard-Rendering/releases) page.
2. Download `rdr2-cbr-v0.1.0-alpha.zip` (includes `rdr2-cbr.asi`, `cbr.ini`, `shaders/`, and `README_INSTALL.txt`) and verify its SHA-256 hash against `SHA256SUMS.txt`.
3. Follow **Step 3: Install into RDR2 from Scratch** below.

### Option B: Build from Source

#### B.1 Software Prerequisites
1. **Visual Studio 2022** (Community or higher) with the **"Desktop development with C++"** workload (C++20).
2. **CMake** (v3.20 or newer).
3. **Vulkan SDK** (1.3.x from [LunarG](https://vulkan.lunarg.com/), optional—provides `glslangValidator` / `dxc` for shader compilation; C++ code itself resolves Vulkan pointers dynamically at runtime).
4. A verified **ASI Loader** (`dinput8.dll`) for RDR2.

#### B.2 Build the ASI Plugin
```powershell
# Clone the repository
git clone https://github.com/ShreyasP10/Checkerboard-Rendering.git
cd Checkerboard-Rendering

# Configure and compile Release x64 (static CRT; builds rdr2-cbr.asi + compiled shaders if glslangValidator/dxc are on PATH)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Build outputs:
- `build/bin/Release/rdr2-cbr.asi` — Compiled ASI plugin binary.
- `build/bin/Release/cbr.ini` — Default configuration file.
- `build/shaders/*.spv` (and `.dxil` when `dxc` is installed) — Compiled compute shaders.

---

### Step 3: Install into RDR2 from Scratch

#### 3.1 Locate Your RDR2 Installation Directory
Find the folder containing `RDR2.exe`:
- **Steam:** Right-click *Red Dead Redemption 2* > **Manage** > **Browse local files** (`...\Steam\steamapps\common\Red Dead Redemption 2\`).
- **Rockstar Games Launcher:** **Settings** > **My installed games** > **Red Dead Redemption 2** > **View installation folder**.
- **Epic Games Store:** Library > **`...`** under RDR2 > **Manage** > folder icon next to *Installation*.

#### 3.2 Install & Verify the ASI Loader
1. Download `dinput8.dll` **only** from the loader project's official HTTPS release page (e.g., [Script Hook RDR2](https://www.dev-c.com/rdr2/scripthookrdr2/) by Alexander Blade). Avoid third-party re-uploads or unverified links.
2. Verify integrity before copying (`Get-FileHash dinput8.dll -Algorithm SHA256` against any official checksum, plus Windows Defender scan).
3. Place `dinput8.dll` directly next to `RDR2.exe`. Remove ASI loaders before launching **Red Dead Online**.

#### 3.3 Deploy CBR Plugin, Config & Shaders
Copy `rdr2-cbr.asi`, `cbr.ini`, and the `shaders/` folder (containing `cbr_reconstruct.spv`) into your RDR2 root directory:

```text
Red Dead Redemption 2/
├── RDR2.exe                     <-- Game executable
├── dinput8.dll                  <-- ASI Loader
├── rdr2-cbr.asi                 <-- Checkerboard Rendering Mod Plugin
├── cbr.ini                      <-- Mod configuration settings
└── shaders/
    └── cbr_reconstruct.spv      <-- Compiled SPIR-V reconstruction shader
```

---

### Step 4: Configure `cbr.ini` Before First Launch

Edit `cbr.ini` for your monitor and GPU (e.g., `3840x2160` for GTX 1070 Ti 4K, or `1920x1080` for Vega 7 APU 1080p):

```ini
[General]
Enabled = true
TargetWidth = 3840           ; Output width (e.g., 3840 for 4K, 2560 for 1440p, 1920 for 1080p)
TargetHeight = 2160          ; Output height (e.g., 2160 for 4K, 1440 for 1440p, 1080 for 1080p)
PreferredApi = Vulkan        ; "Vulkan" (primary active backend), "D3D12", or "Auto"
SamplingStrategy = StrategyA ; "StrategyA" (0.5px viewport/projection jitter) or "StrategyB" (VK_EXT_sample_locations)
MipLodBias = -1.0            ; Applied selectively to anisotropic mipmapped scene samplers (maxLod > 1.0 && anisotropy)

[Reconstruction]
DepthTolerance = 0.010       ; Linearized relative depth disocclusion sensitivity
EnableColorClamping = true   ; Unconditional 3x3 variance clipping (gamma = 1.25)
ColorSpace = YCoCg           ; "YCoCg" (default), "RGB", or "ICtCp" (HDR10 / BT.2100)
HistoryWeight = 0.90         ; Temporal history blend weight on valid reprojected pixels
EnableSpatialFallback = true ; Cardinal cross-bilateral fallback when disoccluded
EnableMotionDilation = true  ; 3x3 closest-depth motion vector dilation over active samples
DepthConvention = Reversed   ; "Reversed" (RDR2 default: 1 near .. 0 far) or "Standard"
DepthNear = 0.1              ; Camera near plane in metres
DepthFar = 0.0               ; Camera far plane in metres (0.0 = infinite far plane for Reversed)
SharpeningStrength = 0.0     ; Optional 5-tap RCAS sharpening strength (0.0..1.0; default 0.0 = disabled)
EnableCameraMotionFallback = false ; Force camera depth unprojection motion fallback when matrices are bound

[Jitter]
JitterPattern = Checkerboard ; 2-phase complementary grid ("Halton" warns & falls back to Checkerboard)
JitterScale = 1.0            ; DEPRECATED: parsed for compatibility but fixed to exact 1 px coverage
JitterDirection = 1          ; Odd-frame horizontal shift direction (+1 or -1)
ProjectionJitterSign = 1     ; Independent sign for projection offsets (+1 or -1)
JitterCompensation = 0.0     ; History reprojection jitter delta multiplier (-1.0 to 1.0, default 0.0)

[Debug]
ShowOverlay = false          ; Toggle runtime HUD telemetry state (F11 / Insert in-game)
DebugView = 0                ; 0=Normal, 1=CBR Mask, 2=Disocclusion, 3=Motion, 4=Raw, 5=Spatial Baseline
LogToFile = true             ; Writes cbr.log in the RDR2 folder
LogLevel = Info              ; Debug, Info, Warning, Error
```

---

### Step 5: Recommended In-Game Graphics Settings (Story Mode)

> **Note:** Claims below about RDR2's internal passes (TAA velocity generation, VRAM headroom under Ultra textures, and Async Compute interplay) reflect architectural expectations calibrated against `v1.0.1491.50` testing; tune per GPU if VRAM-bound.

1. **Display & Window Settings:**
   - **Screen Type:** **Fullscreen**
   - **Resolution:** Native monitor resolution matching `TargetWidth` × `TargetHeight` in `cbr.ini` (**3840×2160** on 4K / GTX 1070 Ti, **1920×1080** on Vega 7 laptops).
   - **V-Sync / Triple Buffering:** **On** (or VRR/FreeSync/G-Sync) for stable frame pacing.
2. **Advanced Graphics API:**
   - **Unlock Advanced Settings:** **Unlocked**
   - **Graphics API:** **Vulkan** (primary active backend; restart RDR2 if switching from DX12).
   - **Async Compute:** **On** (if supported; disable if testing stability baselines).
3. **Anti-Aliasing & Resolution Scaling (Critical!):**
   - **Resolution Scale:** **Off / 1.0×** (*Must stay 1.0× so quarter-res parity maps 1:1 to output pixels*).
   - **TAA:** **Medium** or **High** (ensures motion vector buffers remain active for temporal reprojection).
   - **MSAA:** **2×** (**Required!** In-game MSAA must be set to **2×** so that the scene geometry color and depth attachments render with 2× MSAA / `VK_SAMPLE_COUNT_2_BIT`. `CanCopyToQuarterColor` and `CanCopyToQuarterDepth` enforce 2× MSAA matching `quarterColor` and `quarterDepth`. If MSAA is set to Off (1×), the C-01 safety gate safely fails open and preserves the native image without black-screening, but CBR compute reconstruction will not trigger).
   - **FXAA:** **Off** (recommended Off to prevent post-filtering blurring).
4. **Quality Baseline (4K on 8 GB Pascal GPUs):**
   - Lower **Volumetrics**, **Water**, and **Reflections** to **Medium**; keep **Anisotropic Filtering** at **16×** and reduce **Texture Quality** from **Ultra** to **High** if approaching the 8 GB VRAM ceiling (`300.58 MiB` used by CBR buffers at 4K). On **AMD Vega 7 (512 MB UMA)** laptops, use **1080p** (`75.15 MiB` CBR buffers) and Low/Medium RDR2 presets (`docs/AMD_VEGA_OPTIMIZATION.md`).

---

### Step 6: Verifying Installation via Logs & Debug Modes

#### 6.1 Check `cbr.log`
After launching RDR2 Story Mode, inspect `cbr.log` next to `RDR2.exe`:

```text
=================================================================
 RDR2 Checkerboard Rendering Mod (CBR) Log Initialized           
 Maintainer: Shreyas Pawar                                       
 Target: NVIDIA GeForce GTX 1070 Ti & Vulkan / DX12              
=================================================================
[INFO] Initializing CBREngine for Red Dead Redemption 2...
[INFO] Configuration loaded from cbr.ini (Target: 3840x2160, API: Vulkan, CBR Enabled: true)
[INFO] RenderTargetManager initialized for target: 3840x2160
[INFO] Quarter-Resolution 2x MSAA Buffer size: 1920x1080
[INFO] Total CBR VRAM Footprint: 300.58 MiB (315.19 MB)
[INFO] Vulkan graphics interception is now ACTIVE.
[INFO] Vulkan CBR reconstruction compute pipeline created successfully!
```

#### 6.2 Hotkeys & Debug Visualizations
- Press **`F11`** or **`Insert`** while RDR2 is the active foreground window to toggle `UIOverlay` telemetry (`GetFormattedTelemetry()` logs/renders current frame parity, buffer resolution, VRAM footprint, and hook status; supports custom `HudRenderCallback`).
- Set `DebugView` in `cbr.ini` to inspect individual reconstruction stages:
  - `0` — Normal reconstructed output
  - `1` — Checkerboard parity mask (white = natively shaded this frame, black = temporally reconstructed)
  - `2` — Disocclusion heatmap (green = valid history, red = spatial cross-bilateral fallback)
  - `3` — Motion vector field (`R = Δx`, `G = Δy`)
  - `4` — Unresolved quarter-resolution native buffer
  - `5` — Naive spatial baseline resolve (for A/B quality comparison)

---

### Step 7: Troubleshooting & FAQ

- **Q: The game crashes immediately on startup.**  
  *A:* Confirm you installed a verified `dinput8.dll` matching your RDR2 version. Because `rdr2-cbr.asi` links a static CRT, no external VC++ Redistributable is needed. Inspect `cbr.log` (and crash telemetry from `CrashHandler`) for details.
- **Q: `cbr.log` is never created.**  
  *A:* `dinput8.dll` is missing, blocked by antivirus/SmartScreen, or in the wrong folder. `dinput8.dll`, `rdr2-cbr.asi`, `cbr.ini`, and `shaders/` must sit in the exact same directory as `RDR2.exe`.
- **Q: Image appears blurry or exhibits checkerboard crawling.**  
  *A:* Confirm in-game **Resolution Scale** is **Off / 1.0×**, **TAA** is **Medium/High**, `MipLodBias = -1.0` in `cbr.ini`, and switch between `SamplingStrategy = StrategyA` / `StrategyB` (or adjust `JitterCompensation`) if needed.
- **Q: Can I use this in Red Dead Online?**  
  *A:* **No.** CBR is strictly for offline Story Mode (`CheckAntiCheatModules()` actively detects multiplayer/anti-cheat environments and refuses injection). Always remove `dinput8.dll` and `.asi` plugins before launching Red Dead Online.

---

<a id="configuration-cbrini"></a>
## Configuration (`cbr.ini`)

Authoritative defaults live in [`cbr.ini`](cbr.ini) (`ConfigManager` enforces a 64 KiB file size cap, 500-line / 1024-char limits, `320`–`16384` even-aligned swapchain bounds, and strict integer/float validation):

```ini
[General]
Enabled = true
TargetWidth = 3840
TargetHeight = 2160
PreferredApi = Vulkan
SamplingStrategy = StrategyA
MipLodBias = -1.0

[Reconstruction]
DepthTolerance = 0.010
EnableColorClamping = true
ColorSpace = YCoCg
HistoryWeight = 0.90
EnableSpatialFallback = true
EnableMotionDilation = true
DepthConvention = Reversed
DepthNear = 0.1
DepthFar = 0.0
SharpeningStrength = 0.0
EnableCameraMotionFallback = false

[Jitter]
JitterPattern = Checkerboard
JitterScale = 1.0
JitterDirection = 1
ProjectionJitterSign = 1
JitterCompensation = 0.0

[Debug]
ShowOverlay = false
DebugView = 0
LogToFile = true
LogLevel = Info
```

---

## Testing

The portable C++ test suite (`cbr_tests`, `cbr_engine_tests`), headless Vulkan compute reconstruction test (`cbr_vulkan_headless`, executed against Mesa Lavapipe in CI), mathematical shader-mapping proof, shader compilers (`glslangValidator` / `dxc`), and MinGW-w64 cross-syntax checks mirror [`.github/workflows/build.yml`](.github/workflows/build.yml):

```bash
# Validate GLSL compute shaders & HLSL compute shader into shaders/spv/
mkdir -p shaders/spv
glslangValidator -V shaders/cbr_reconstruct.comp -o shaders/spv/cbr_reconstruct.spv
glslangValidator -V shaders/cbr_reconstruct_quad.comp -o shaders/spv/cbr_reconstruct_quad.spv
glslangValidator -V shaders/cbr_resolve_simple.comp -o shaders/spv/cbr_resolve_simple.spv
glslangValidator -D -e CSMain -S comp -V shaders/cbr_reconstruct.hlsl -o shaders/spv/cbr_reconstruct_hlsl.spv
# When Microsoft DXC is installed:
dxc -T cs_6_0 -E CSMain shaders/cbr_reconstruct.hlsl -Fo shaders/spv/cbr_reconstruct.dxil

# Configure & run unit tests + headless Vulkan compute test (requires Vulkan ICD / mesa-vulkan-drivers)
cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --target cbr_tests cbr_engine_tests cbr_vulkan_headless
ctest --test-dir build-tests --output-on-failure -V

# Verify 4-quadrant MSAA coverage equivalence across all 36,864 parity/shift/resolution cases
python3 tests/check_shader_mapping.py
```

```bash
# Strict cross-compile syntax check for all 13 Windows/portable translation units
for f in logger config jitter_manager render_target_manager reconstruction_pass ui_overlay cbr_engine hooks main hooks_vulkan hooks_dx12 crash_handler pattern_scanner; do
  x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -Wall -Wextra -Wpedantic -Wshadow -Iinclude -Ivendor/minhook/include src/$f.cpp
done
```

---

<a id="changelog"></a>
## Changelog / Recent Updates

### Current (`feature/cbr-pipeline`)
* **Phases 1–7 Pipeline Completion, Security Hardening & Headless Vulkan Verification:**
  * Shaders (`C-05`, `H-05`, `M-01`, `M-02`, `M-07`, `M-08`, Sections 5.4 & 5.6): Quarter-res velocity coordinate downshift (`>> 1`), double-guarded NaN/Inf sanitization, HLSL `g_HistoryDepth.Load` + `cardinalColor[4]` caching, thread-local 2×2 quad register sharing (`cbr_reconstruct_quad.comp` without subgroup extensions), `binding = 7` / `register(b1)` camera-only depth unprojection fallback, and optional 5-tap RCAS sharpening (`SharpeningStrength`, default `0.0`).
  * Vulkan & DX12 Runtime (`C-01..C-04`, `H-02`, `H-03`, `H-06`, `M-03..M-05`): Framebuffer/render-pass attachment inspection (`draws >= 250`), `vkAcquireNextImage2KHR` tracking, ping-pong descriptor sets (`historyColorViewA`/`B`), tracked-layout pre-dispatch & zero-copy barriers, intermediate scene color blit without overwriting UI/HUD overlays, `VkQueryPool` GPU duration timing, selective anisotropic `MipLodBias`, scoped hook teardown, and full D3D12 command queue/list VMT hooks + committed resource/PSO dispatch.
  * Hardening, Overlay & CI (`H-01`, `H-04`, Sections 4.1–4.3, 5.10, 7): Non-accumulating tracked projection jitter cache, online CLI launch-argument safety gate (`-rdo`, `-scOnlineOnly`, `-Multiplayer`), preloaded `dbghelp.dll` minidump resolution in `CrashHandler::Install()`, region-by-region `VirtualQuery` in `PatternScanner::ScanModule`, built-in `DefaultTelemetryHudCallback`, hardened `check_shader_mapping.py`, and headless Vulkan Lavapipe compute test (`tests/test_vulkan_headless.cpp`) in CMake and GitHub Actions CI.
* **Architecture Features (`bb85392`):**
  * **AVX2 SIMD Pattern Scanner:** Runtime CPUID AVX2 detection (`_mm256_cmpeq_epi8` 32-byte scanning) with scalar fallback in `pattern_scanner.cpp`.
  * **HDR10 `ICtCp` Clamping:** Added perceptual BT.2100 `ColorSpace::ICtCp` (`u_ColorSpace = 2`) across config and GLSL/Quad/HLSL compute shaders.
  * **Interactive HUD Control API (`UIOverlay`):** Added live tuning setters (`SetHistoryWeight`, `SetDepthTolerance`, `SetMipLodBias`, `SetDebugView`, `SetColorSpace`, etc.) and pluggable `HudRenderCallback`.
* **Code Hardening, Depth & Mapping (`94c4819`, `3d741b4`, `d20eb79`, `80382d5`):**
  * Centralized limits into [`include/cbr/limits.h`](include/cbr/limits.h), hardened depth range sanitization (`depth_convention.h`), resolved the 19-finding concurrency/shader/ABI audit, and synchronized all 57 tracked files into [`CODEBASE.md`](CODEBASE.md).

### `v0.1.0-alpha`
* Implemented Vulkan MinHook detours (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, `vkCmdSetSampleLocationsEXT`), `StrategyA` vs. `StrategyB` mutual exclusivity, `cbr_reconstruct_quad.comp`, unconditional `gamma = 1.25` variance clipping, `MipLodBias = -1.0`, and automated GitHub Actions Release packaging (`rdr2-cbr-release.zip` + `SHA256SUMS.txt`).

---

## Contributing

Contributions, reverse-engineering notes, and shader benchmarks are welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for C++20, GLSL `#version 450`, and HLSL SM 6.0 style guidelines and our strict offline-only contribution policy.

---

## Collaborators & Maintainers

- **Shreyas Pawar** – Project Lead, Co-Owner & Graphics Architecture

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Disclaimer

This mod is for **educational, experimental, and research purposes only**. It is not affiliated with, endorsed by, or associated with Rockstar Games or Take-Two Interactive. Use strictly in offline single-player mode.
