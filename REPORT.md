# RDR2 Checkerboard Rendering (CBR)
## Final Architectural Specification & Review Report

**Date:** October 2026
**Project Lead:** Shreyas Pawar
**Target Environment:** Red Dead Redemption 2 (Vulkan / DX12 ASI Mod), targeting 4K reconstruction on Pascal architectures (e.g., GTX 1070 Ti).

---

## 1. Product Requirements Document (PRD)
**Objective:** Deliver a high-performance temporal checkerboard rendering upscaler (ASI plugin) to achieve 4K visual fidelity from a 1080p native render target, doubling framerates on older GPU hardware lacking tensor cores.
**Scope:** A local ASI/DLL single-player mod. No networked components, SQL, or HTTP traffic. Focus is entirely on local memory injection and GPU compute scheduling.
**Target Audience:** Users of legacy hardware wanting modern sub-pixel upscaling tech without purchasing DLSS/FSR hardware.

## 2. System Requirements Specification (SRS)
* **OS:** Windows 10/11 64-bit.
* **Graphics API:** Vulkan 1.2/1.3 or DirectX 12 Feature Level 12_0.
* **Game Engine:** RAGE Engine (RDR2 specific offsets).
* **Dependencies:** `dinput8.dll` (ASI Loader), MinHook (integrated).
* **Shader Languages:** GLSL (Compute), HLSL (Compute Shader Model 6.0).

## 3. Technical Requirements Document (TRD) & Architecture
The plugin intercepts the presentation layer and command recording passes of the RAGE Engine to downscale geometry passes and execute the GPU compute pass.
* **ConfigManager:** Thread-safe singleton parsing `cbr.ini` with strict bounds checking and sanitized input.
* **HooksManager:** MinHook inline detours on `vulkan-1.dll`:
  - `vkQueuePresentKHR` & `vkCreateSwapchainKHR` for swapchain and presentation coordination.
  - `vkCmdSetViewport` & `vkCmdSetScissor` for dynamic 4K -> 1080p 2× MSAA geometry downscaling while preserving native 4K UI/HUD passes.
  - `vkCmdBeginRenderPass` & `vkCmdEndRenderPass` for mid-frame scene pass completion interception.
  - `vkCmdSetSampleLocationsEXT` for hardware programmable sample location toggling.
* **SamplingStrategy:** Configurable isolation between **Strategy A** (±0.5px horizontal viewport jitter) and **Strategy B** (`VK_EXT_sample_locations` with stationary viewport) to prevent sample cancellation.
* **PatternScanner:** IDA-style wildcard AOB scanner with RIP-relative resolution and SEH memory protection to automatically discover camera projection matrices.
* **RenderTargetManager:** Manages Vulkan/DX12 VRAM allocations (~300.59 MiB for 4K CBR).
* **ReconstructionPass:** 
  - Manages internal `VkDescriptorPool` and 7-binding `VkDescriptorSet` (MSAA color/depth, history color/depth, velocity, output storage).
  - Synchronizes execution using explicit `vkCmdPipelineBarrier` memory transitions.
  - Dispatches `cbr_reconstruct.comp` (or `cbr_reconstruct_quad.comp` using `GL_KHR_shader_subgroup_quad`).
  - Implemented closest-depth motion-vector dilation, temporal history reprojection, depth delta disocclusion testing, and YCoCg neighborhood variance clipping.

## 4. Software Requirements Document (SRD) - Security & Robustness
The latest audit (v0.1.0-alpha) identified and resolved key operational risks for ASI mods:
* **Thread Safety:** Lock-free reads and exclusive write locks across render targets and configuration singletons prevent frame-tearing and data races during rendering.
* **NaN Poisoning:** Reconstructed pixels are mathematically guarded against `NaN`/`Inf` injection. A single NaN in the velocity vector or depth buffer is gracefully discarded, preventing the history ping-pong buffer from permanently black-screening.
* **Loader-Lock Safety:** The `DllMain` entry point delegates all initialization to lazy hooks, strictly adhering to Windows API best practices and preventing startup deadlocks.

## 5. Development Plan (DEV_PLAN) & Changelog
* **Phase 1 (Complete):** Core architecture, config systems, memory allocation logic, and robust temporal reconstruction shaders (GLSL & HLSL).
* **Phase 2 (Complete):** Live Vulkan MinHook detours (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, `vkCmdSetSampleLocationsEXT`), dynamic resolution redirection, Strategy A & B sampling, AOB pattern scanner, descriptor pool/set allocation, barrier synchronization, and GitHub CI/CD automated release packaging (`v0.1.0-alpha`).
* **Phase 2.6 & Phase 3 (Active Focus):** Real-time Dear ImGui telemetry overlay (F11/Insert toggle) and DirectX 12 parity hooking via dummy device swapchain VMT method.

## 6. Risk Register
| Risk | Impact | Mitigation Status |
| :--- | :--- | :--- |
| **Data Races on Config** | High (Crash) | Replaced exposed pointers with lambda-based `Modify()` locks. |
| **NaN Shader Poisoning** | High (Visual) | Implemented strict `isinf`/`isnan` bounds in GLSL/HLSL and C++ host. |
| **DllMain Deadlock** | Critical (Crash) | Deferred `CreateThread` and hooks to lazy initialization. |
| **Config Injection (cbr.ini)** | Low (OOM/PII) | Strict bounds check (64KB limit, line count limit) and sanitization applied. |
| **Supply Chain Hijacking** | High (Malware) | Pinned GitHub actions, minimized `contents:write` scopes. |
| **RAGE Custom Suballocators** | High (Hook Miss) | Detoured command buffer viewports & render passes instead of raw image creation. |
| **Game Update Invalidation** | High (Crash) | Implemented SEH-safe wildcard AOB pattern scanner (`pattern_scanner.cpp`). |

---
*Report generated and validated for v0.1.0-alpha codebase.*
