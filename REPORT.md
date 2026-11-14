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

## 7. Feature Expansion Plan & Audit Resolution Status (CODEBASE_2.md)

* **Full Vulkan Resource Pipeline:** **Present & Hardened** — Descriptor ping-ponging (C-02 Resolved), initial layout transitions & history clearing (C-03 Resolved), framebuffer/image-view tracking (`vkCreateImage`, `vkCreateImageView`, `vkCreateFramebuffer`, `VkRenderPassAttachmentBeginInfo`), and `CopyImageRegion` / `BlitToImage` are fully implemented.
  - *C-01 Resolved:* `quarterDepth` allocated as `VK_FORMAT_D32_SFLOAT`, gating `OnScenePassEnd`/`BlitToImage` on `colorCopied && depthCopied` to fail open safely if MSAA is disabled.
  - *C-02 Resolved:* Handled 2× MSAA passes without inline 1× resolve attachments via separate blit/resolve path.
* **Scene Pass Identification:** **Present (a)** — `Hooked_vkCreateRenderPass` and `Hooked_vkCreateRenderPass2`/`KHR` inspect attachment formats and sample counts (`RecordTrackedRenderPass`), and `Hooked_vkCmdEndRenderPass` gates on `rpCandidate && draws >= 250 && !colorIsSwapchain`.
* **DirectX 12 Parity:** **Present & Hardened** — `CreateDX12Pipeline` (RootSignature + Compute PSO + runtime D3DCompile fallback + b1 camera CBV), `DispatchDX12` (resource barriers + descriptor table binding), and command-list VMT hooks (`ExecuteCommandLists`, `RSSetViewports`, `RSSetScissorRects`, `OMSetRenderTargets`, `DrawInstanced`, `DrawIndexedInstanced`) are implemented.
  - *C-03 Resolved:* Saves and restores game descriptor heaps and compute PSO before and after CBR compute dispatch.
  - *M-03 Resolved:* Fast-path guard in `Hooked_D3D12ExecuteCommandLists` bypasses redundant COM interface queries on the hot path.
  - *M-04 Resolved:* Secure `LoadLibraryExA(..., nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` locks DLL resolution for `d3d12.dll` and `d3dcompiler_47.dll` to System32.
* **Motion Vector Fallback (Camera-Only Depth Unprojection):** **Present & Guarded** — `CBRCameraUniforms` UBO (binding = 7 / register(b1)), `InvertMatrix4x4`, and `SetCameraMatrices` are implemented.
  - *M-02 Resolved:* Perspective cotangent bounds validation in `PatternScanner::GetCameraProjectionMatrix` ($0.2 \le \cot \le 5.0$) safely falls back to pure Strategy A viewport shift if camera scanning matches stale or dead memory.
* **In-Game ImGui Overlay:** **Partially Present (b)** — Telemetry state management, hotkeys, and `GetFormattedTelemetry()` are present; Dear ImGui draw backend remains planned for Phase 3 (H-03).
* **Contrast Adaptive Sharpening (CAS / RCAS):** **Present (a)** — `SharpeningStrength` (0.0..1.0) parsed in `config.cpp` and uploaded to the GPU via `CBRCameraUniforms::sharpeningStrength`.
* **HDR / scRGB Support:** **Present (a)** — Full ICtCp (ITU-R BT.2100), YCoCg, and RGB color clamping support.
* **Asynchronous Compute:** **Missing — Optional (d)** — Inline compute execution on the graphics command buffer remains appropriate given immediate downstream post-FX dependencies.
* **Halton Jitter:** **Intentionally Omitted (d)** — Properly rejected in favor of 2-phase 2× MSAA Checkerboard coverage.
* **Automated Headless Rendering Test:** **Present (a)** — Added `tests/test_vulkan_headless.cpp` (`cbr_vulkan_headless` CTest target in `CMakeLists.txt`) with POSIX `libvulkan.so.1` dynamic loader support in `reconstruction_pass.cpp` and `render_target_manager.cpp`.

## 8. Verification Checklist & Current Milestone Evaluation (Section 6)

### 8.1 Automated Test Suite & CI Baseline (100% Green)
The headless verification pipeline executes in GitHub Actions CI using headless software Vulkan (Mesa Lavapipe):
* **Execution Command:** `cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure`
* **Test Results (5/5 Passing, 0 Failed):**
  1. `cbr_core` — Unit tests covering configuration parsing, logger buffering, thread-safe access, jitter calculation, and math invariants.
  2. `cbr_engine` — Lifecycle tests verifying engine initialization, swapchain transitions, and graphics API detection.
  3. `cbr_engine_auto` — API autodetection tests selecting Vulkan or DX12 based on active module exports.
  4. `cbr_vulkan_headless` — Headless Vulkan compute execution allocating resources, binding descriptors, and dispatching `cbr_reconstruct.comp` via Lavapipe.
  5. `cbr_shader_mapping` — Mathematical verification across 36,864 coordinate combinations, 100% 4-quadrant sample coverage, and 272 velocity shift/clamp boundary tests.
* **Compiler & Syntax Verification:** Verified under GCC/Clang with `-Wall -Wextra -Wpedantic`, MinGW cross-compilation syntax checks, and MSVC Release builds with security flags enabled.

### 8.2 In-Engine Verification & Critical Defect (C-01) Guardrails
* **`quarterDepth` Format Alignment:** `res.quarterDepth` in `RenderTargetManager::CreateVulkanResources` is allocated as `VK_FORMAT_D32_SFLOAT` (126) with aspect mask `VK_IMAGE_ASPECT_DEPTH_BIT` (2u). This matches RDR2's scene depth buffer format, satisfying `VUID-VkImageCopy-aspectMask-00142`.
* **Execution Copy Gating:** `CBREngine::OnScenePassEnd` and `ReconstructionPass::BlitToImage` are strictly gated on `if (copiedColor && copiedDepth)`.
  - When in-game MSAA is set to 2× in RDR2 graphics settings, `colorMeta.samples == 2u` and `depthMeta.samples == 2u`, allowing both `vkCmdCopyImage` calls to succeed and passing execution to the compute pass.
  - If in-game MSAA is Off (1×), the copy gates fail open, bypassing reconstruction and blitting, leaving native rendering completely intact without visual corruption or black screens.

### 8.3 RenderDoc Frame Capture Audit Protocol
When validating captured frames from RDR2 Story Mode via RenderDoc at the main geometry pass boundary (`vkCmdEndRenderPass`, $\ge 250$ draw calls):
1. **Step 1 (Copy-In Verification):**
   - Inspect the three `vkCmdCopyImage` invocations immediately following `vkCmdEndRenderPass`.
   - Confirm that `res.quarterColor` (1920×1080 2× MSAA HDR), `res.quarterDepth` (1920×1080 `D32_SFLOAT`), and `res.velocity` (1920×1080 `RG16_SFLOAT`) contain valid scene data.
2. **Step 2 (Ping-Pong Compute Dispatch Verification):**
   - Inspect `vkCmdDispatch(240, 135, 1)`.
   - Confirm that Set 0 writes to `historyColor[1]` / `historyDepth[1]` on even frames ($N \pmod 2 = 0$).
   - Confirm that Set 1 writes to `historyColor[0]` / `historyDepth[0]` on odd frames ($N \pmod 2 = 1$).
   - Verify that history layouts transition smoothly between `SHADER_READ_ONLY_OPTIMAL` and `GENERAL`.
3. **Step 3 (Blit-Out Verification):**
   - Inspect `vkCmdBlitImage` copying `historyColor[justWrittenIdx]` into the game's offscreen scene color target (`COLOR_ATTACHMENT_OPTIMAL`).
   - Confirm that subsequent full-resolution post-processing, TAA, depth of field, and 4K UI rendering passes operate seamlessly on the reconstructed 4K buffer.

### 8.4 Milestone Progress & What Was Improved
Since the initial prototype release, the CBR pipeline has undergone extensive architectural hardening:
* **Eliminated History Desynchronization (C-02 Resolved):** Proper binding of ping-pong storage descriptors closed the temporal feedback loop, preventing history freeze.
* **Eliminated Uninitialized Layout Crashes (C-03 Resolved):** All Vulkan images transition out of `UNDEFINED` on creation with history buffers explicitly cleared to zero.
* **Zero Jitter Drift (H-01 Resolved):** Projection matrix tracking ensures subpixel offsets are applied idempotently without cumulative distortion.
* **Discrete Multi-GPU Protection (M-01 Resolved):** Multi-device systems dynamically resolve physical device properties to consistently prioritize high-performance discrete GPUs over integrated chips.
* **Stale Matrix Protection (M-02 Resolved):** Cotangent sanity checks prevent uninitialized or non-perspective memory pointers from dropping jitter, seamlessly defaulting to viewport redirection.
* **Zero Hot-Path COM Overhead (M-03 Resolved):** Fast-path device caching skips redundant query interfaces on every command queue submission.
* **Hardened Security Boundaries (M-04 Resolved):** All external DLL dependencies are strictly constrained to `System32`, eliminating DLL search-order planting vectors.

---
*Report updated and validated for CODEBASE_2.md and Section 6 Verification Checklist.*
