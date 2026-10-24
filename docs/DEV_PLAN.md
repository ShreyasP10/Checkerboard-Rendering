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

### Phase 2.6 & Phase 3: Telemetry, In-Game Overlay & DirectX 12 Parity [🔄 ACTIVE FOCUS]
- **Status:** **In Progress**
- **Duration:** 2 Weeks
- **Objectives:**
  - Complete interactive Dear ImGui overlay integration (F11/Insert toggle) for real-time telemetry, frame time metrics, and debug visualization modes.
  - Implement DirectX 12 hooking parity via dummy device swapchain VMT method (`IDXGISwapChain3::Present` VMT index 8).
  - Execute HLSL compute shader (`cbr_reconstruct.hlsl`) on DX12 command queue.

### Phase 3: Projection Jitter & Quarter-Resolution Rendering
- **Duration:** 2 Weeks
- **Objectives:**
  - Patch camera projection matrix with subpixel alternating jitter offsets.
  - Ensure UI, minimap, and HUD passes remain unjittered.
  - Inject `-0.5` MIP LOD bias across all scene texture samplers.
  - Render the scene into quarter-resolution MSAA targets.
- **Deliverables:**
  - Half-shading-cost scene rendering with alternating subpixel grid alignments.

### Phase 4: Compute Reconstruction Shader Pipeline
- **Duration:** 6–8 Weeks (Critical Path)
- **Objectives:**
  - Author GLSL compute shader (`cbr_reconstruct.comp`) and HLSL equivalent (`cbr_reconstruct.hlsl`).
  - Implement 2× MSAA sample unpack and checkerboard parity test.
  - Implement temporal reprojection using screen-space velocity vectors.
  - Implement depth-delta disocclusion test with spatial bilateral fallback.
  - Implement YCoCg 3×3 color bounding box clamping to suppress ghosting.
- **Deliverables:**
  - Fully functional 4K reconstruction compute pass operating in under 2 ms on GTX 1070 Ti.

### Phase 5: Post-Processing & TAA Integration
- **Duration:** 3 Weeks
- **Objectives:**
  - Insert reconstruction pass output before tone mapping, bloom, depth-of-field, and UI composition.
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

| Milestone | Target Completion | Critical Dependency |
|---|---|---|
| **M1: Hook & Overlay Active** | Month 1 | MinHook & Vulkan Loader hook stability |
| **M2: Quarter-Res Target Intercept** | Month 2 | Stable RAGE render target signatures |
| **M3: Jitter & LOD Bias Verified** | Month 2.5 | Camera matrix offset verification |
| **M4: First CBR Reconstruction** | Month 4.5 | Motion vector extraction or fallback |
| **M5: Post-Processing Clean** | Month 5.5 | Proper render graph insertion order |
| **M6: Pascal Optimization Complete** | Month 6.5 | Nsight profiling & register reduction |
| **M7: Release v1.0** | Month 7 | Beta community testing |
