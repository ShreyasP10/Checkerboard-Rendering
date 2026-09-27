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

### Phase 1: Hooking Infrastructure & ASI Plugin Framework
- **Duration:** 4 Weeks
- **Objectives:**
  - Build the ASI DLL entry point with `dinput8` loader compatibility.
  - Implement MinHook wrapper for Vulkan loader functions (`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`).
  - Implement DirectX 12 VMT hooking for `IDXGISwapChain3::Present` and command queues.
  - Implement thread-safe diagnostic logger (`cbr.log`) and config parser (`cbr.ini`).
  - Integrate Dear ImGui overlay hooked into the swapchain render pass.
- **Deliverables:**
  - Functional `rdr2-cbr.asi` loading into RDR2, logging draw calls, and rendering ImGui overlay without crashing.

### Phase 2: Render Target Interception & Memory Management
- **Duration:** 3 Weeks
- **Objectives:**
  - Intercept creation of full-resolution color and depth buffers in Vulkan/D3D12.
  - Allocate quarter-resolution 2× MSAA intermediate targets.
  - Implement custom device-local VRAM sub-allocator for ping-pong history textures.
  - Verify memory footprint remains strictly under 300 MB.
- **Deliverables:**
  - Active render target redirection layer verified in RenderDoc.

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
