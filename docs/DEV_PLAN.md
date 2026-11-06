# Development Plan (DEV_PLAN)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** DEV-RDR2CBR-001  
**Version:** 1.0  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. Project Roadmap & Phase Overview

The development roadmap is structured into 8 distinct phases designed to de-risk technical blockers incrementally, beginning with engine reverse engineering and Vulkan/DX12 hook verification before shader authoring.

```
[Phase 0: Research & RE] ──> [Phase 1: Hook Framework] ──> [Phase 2: Target Intercept]
                                                                     │
[Phase 5: Post-Proc Integration] <── [Phase 4: Reconstruction] <── [Phase 3: Jitter & Render]
         │
         v
[Phase 6: Pascal Optimization] ──> [Phase 7: Polish & Release]
```

---

## 2. Phase-by-Phase Breakdown

### Phase 0: Research & Reverse Engineering
- **Duration:** 4–6 Weeks
- **Objectives:**
  - Map RAGE engine render passes via RenderDoc / Nsight Graphics captures on RDR2.
  - Locate camera projection matrices, view matrices, and uniform buffer binding points.
  - Analyze internal velocity/motion vector buffer generation and storage formats.
  - Verify whether companion app string `PostFX::g_CheckerBoardEnable` references an active console code branch in PC binaries.
- **Deliverables:**
  - Memory offset documentation sheet for RDR2 build 1436.28+.
  - RenderDoc frame capture analysis report.

### Phase 1: Hooking Infrastructure & ASI Plugin Framework [✅ VERIFIED IN-GAME]
- **Status:** **Completed & Validated in RDR2 build 1.0.1491.50** (See [Test Report](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/docs/IN_GAME_TEST_REPORT_v1491.md))
- **Duration:** 4 Weeks
- **Objectives Accomplished:**
  - Built ASI entry point (`rdr2-cbr.asi`) with Alexander Blade `dinput8` and LML compatibility.
  - Implemented module pinning to prevent loader lock unloading crashes.
  - Implemented thread-safe diagnostic logger (`cbr.log`) and configuration parser (`cbr.ini`).
  - Added full crash handler with automated minidump generation (`cbr_crash_*.dmp`).
  - Validated 2× MSAA quarter-resolution memory allocation footprint (300.59 MiB at 4K).
  - Verified graceful fallback protection (mod times out quietly after 60s without crashing host).

### Phase 2: Active Graphics Detours & Render Target Interception [✅ COMPLETED]
- **Status:** **Completed & CI Verified** (See [Next Implementation Phase Plan](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/nextImplementationPhase.md) and [Vulkan Hooking Guide](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/docs/VULKAN_HOOKING_GUIDE.md))
- **Duration:** 3 Weeks
- **Objectives Accomplished:**
  - Transitioned from stubs to active MinHook detours on `vulkan-1.dll` (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, `vkCmdSetSampleLocationsEXT`).
  - Captured swapchain backbuffers and wired real `VkDevice` into `ReconstructionPass`.
  - Implemented dynamic viewport/scissor downscaling: redirects 4K scene passes to 1080p 2× MSAA while keeping UI/HUD overlays at native 4K.
  - Implemented mid-frame scene pass interception on `vkCmdEndRenderPass` to trigger compute reconstruction before post-processing and UI compositing.
  - Implemented Strategy A (0.5px horizontal viewport jitter) and Strategy B (`VK_EXT_sample_locations` alternation with stationary viewport) with strict mutual exclusivity.
  - Authored IDA-style AOB camera pattern scanner (`pattern_scanner.cpp`) with RIP-relative address resolution and SEH memory protection fallback.
  - Built internal Vulkan descriptor pool, descriptor sets (bindings 0–6), and `vkCmdPipelineBarrier` synchronization.
- **Deliverables:**
  - Active Vulkan graphics detours, complete compute pipeline dispatch, and automated CI release packaging (`rdr2-cbr-release.zip`).

### Phase 3: Projection Jitter, Limits Centralization & Quarter-Resolution Rendering [✅ COMPLETED]
- **Status:** **Completed & Verified**
- **Duration:** 2 Weeks
- **Objectives Accomplished:**
  - Implemented 2-phase checkerboard jitter sequence in `JitterManager` with non-accumulating `SetProjectionJitter`.
  - Unified all hardware, configuration, buffer, and variance clipping constants into `include/cbr/limits.h` (`kVarianceClipGamma = 1.25f`, `kMinSwapchainExtent = 320`, `kMaxSwapchainExtent = 16384`).
  - Standardized texture MIP LOD bias to `-1.0f` to compensate for quarter-resolution 2× MSAA.
  - Eliminated lock-order inversion between `JitterManager` and `ConfigManager` to guarantee deadlock-free frame updates.
  - Formally proven 100% 4-quadrant geometric coverage across alternating frames (verified via `tests/check_shader_mapping.py` across 36,864 coordinate permutations).
- **Deliverables:**
  - Jitter subsystem, projection offsets, centralized limits header, and unit tests.

### Phase 4: Compute Reconstruction Shader Pipeline [✅ COMPLETED]
- **Status:** **Completed & CI Verified**
- **Duration:** 6–8 Weeks
- **Objectives Accomplished:**
  - Authored full single-pixel GLSL compute shader (`cbr_reconstruct.comp`, 16×16 workgroup) and HLSL equivalent (`cbr_reconstruct.hlsl`).
  - Authored quad-based reconstruction compute shader (`cbr_reconstruct_quad.comp`, 8×8 workgroup, 1 Quad / 4 pixels per thread) to maximize texture cache hit rates.
  - Implemented 2× MSAA sample unpack matching hardware grid geometries.
  - Implemented linearized depth disocclusion testing (`LinearizeDepth()`, `RelativeDepthDelta()`) supporting Reversed-Z with infinite far plane.
  - Implemented variance color clipping ($\mu \pm 1.25\sigma$) in YCoCg space to suppress ghosting while preserving sharp edges.
  - Removed problematic `&& isMoving` guard to eliminate static geometry ghosting and TAA checkerboard shimmer.
  - Implemented all 6 debug visualization modes (0: Normal, 1: Mask, 2: Disocclusion, 3: Motion, 4: Raw, 5: Spatial Baseline Resolve) across all shaders.
  - Added NaN/Inf guards for depth averaging, history reprojection, and depth buffer outputs.
- **Deliverables:**
  - Complete SPIR-V and HLSL compute reconstruction pipeline with unit and equivalence test suites.

### Phase 4.5: Codebase Audit & System Hardening [✅ COMPLETED]
- **Status:** **Completed (Commit `80382d5`)**
- **Objectives Accomplished:**
  - Resolved all 19 findings identified during comprehensive code audit.
  - Added compile-time static assertion on `MinimalVkSwapchainCreateInfoKHR::imageExtent` byte offset (44 on 64-bit) and validated `sType`.
  - Moved `m_initialized` visibility flag inside `unique_lock` in `RenderTargetManager` to prevent memory ordering races.
  - Eliminated render-thread per-message disk I/O stalls by restricting logger `flush()` to Warning and Error levels.
  - Hardened config parser, logger string truncation, and plugin initialization gating.

### Phase 5 & Phase 2.6: Post-Processing, In-Game Overlay & DirectX 12 Parity [🔄 ACTIVE FOCUS]
- **Status:** **In Progress**
- **Duration:** 3 Weeks
- **Objectives:**
  - Insert reconstruction pass output before tone mapping, bloom, depth-of-field, and UI composition in live game render graph.
  - Complete interactive Dear ImGui overlay integration (F11/Insert toggle) for real-time telemetry, frame time metrics, and debug visualization modes.
  - Complete DirectX 12 hooking parity via dummy device swapchain VMT method (`IDXGISwapChain3::Present` VMT index 8).
  - Tune interaction between CBR reconstruction and RDR2's native TAA pass.
  - Address edge artifacts around alpha-tested foliage, fur, and horse tails.
- **Deliverables:**
  - Clean image composition without flickering or halos around alpha assets.

### Phase 6: Pascal & GTX 1070 Ti Hardware Optimization
- **Duration:** 2 Weeks
- **Objectives:**
  - Profile kernel execution using NVIDIA Nsight Graphics on GP104.
  - Optimize shared memory tiling (18×18 tile with border apron).
  - Minimize register pressure to ensure 100% occupancy (≤ 32 VGPRs).
  - Verify 15%+ frame-time reduction vs native 4K.
- **Deliverables:**
  - High-occupancy, low-latency compute kernel achieving target KPIs.

### Phase 7: Polish, Packaging & Release
- **Duration:** 2 Weeks
- **Objectives:**
  - Finalize configuration interface in ImGui overlay and `cbr.ini`.
  - Comprehensive documentation, setup guides, and troubleshooting manuals.
  - Community beta testing on various Pascal and non-Pascal GPUs.
- **Deliverables:**
  - Public GitHub repository release with pre-compiled binaries and source code.

---

## 3. Milestones & Timeline Summary

| Milestone | Status | Critical Dependency / Verification |
|---|---|---|
| **M1: Hook & Lifecycle Framework** | **COMPLETED** | Verified in RDR2 build 1.0.1491.50 |
| **M2: Quarter-Res Target Intercept & Redirection** | **COMPLETED** | MinHook detours on viewport & end render pass |
| **M3: Jitter & LOD Bias (-1.0f) Verified** | **COMPLETED** | Tested via `test_core.cpp` & `check_shader_mapping.py` |
| **M4: Compute Reconstruction Pass (Single & Quad)** | **COMPLETED** | GLSL / HLSL shaders with variance clipping & 6 debug modes |
| **M4.5: Codebase Audit & System Hardening** | **COMPLETED** | 19 audit findings resolved (commit `80382d5`) |
| **M5: Post-Processing & In-Engine TAA Interplay** | **IN PROGRESS** | Proper render graph insertion order |
| **M6: Pascal & APU Hardware Optimization** | **IN PROGRESS** | Nsight profiling, quad shader evaluation, ASO mode |
| **M7: Public Release v1.0** | Planned | Beta community testing |
