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
* **RenderTargetManager:** Manages Vulkan/DX12 VRAM allocations (237.30 MiB / 248.83 MB for 4K CBR; auxiliary output storage buffers omitted via Item M-06).
* **ReconstructionPass:** 
  - Manages internal `VkDescriptorPool` and 8-binding `VkDescriptorSet` (bindings 0–6 for MSAA color/depth, history color/depth, velocity, and output storage; binding 7 for `CBRCameraData` UBO).
  - Synchronizes execution using explicit `vkCmdPipelineBarrier` memory transitions.
  - Dispatches `cbr_reconstruct.comp` (or `cbr_reconstruct_quad.comp` using thread-local quad register sharing without requiring subgroup extensions).
  - Implemented closest-depth motion-vector dilation, temporal history reprojection, depth delta disocclusion testing, and YCoCg / RGB / ICtCp neighborhood variance clipping.

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
| **CET / CFG Incompatibility** | Medium (Crash) | Configured `/guard:cf` compiler CFG and `/CETCOMPAT:NO` linker flag for x64 MinHook trampolines (RSK-12). |
| **Late-Loaded Anti-Cheat Modules** | High (Ban) | Added periodic module scanning in `OnPostPresent` every 300 frames with automatic self-disabling and history clearing (RSK-13). |

## 7. Feature Expansion Plan & Audit Resolution Status (Sections 4 & 5 Hardening)

* **Security & Hardening Suite (Section 4):**
  - *4.1 Crash Handler Async-Safety:* Wrapped filter body in MSVC SEH `__try / __except (EXCEPTION_EXECUTE_HANDLER)`. Skipped minidump creation for `0xC0000374` (`STATUS_HEAP_CORRUPTION`) to prevent deadlocks or secondary crashes when traversing a corrupted heap.
  - *4.2 Pattern Scanner Containment:* Added module boundary verification using `GetModuleHandleExA(FROM_ADDRESS)` in `PatternScanner::ResolveRipRelative` to prevent jumps outside mapped module sections; documented non-SEH fallback for GCC/MinGW.
  - *4.3 DLL Search Order Isolation:* Enforced `LOAD_LIBRARY_SEARCH_SYSTEM32` for `dbghelp.dll`, `d3d12.dll`, and `d3dcompiler_47.dll`.
  - *4.4 Anti-Cheat Coexistence:* Hardened launch check and added 300-frame periodic scan in `CBREngine::OnPostPresent` to disable CBR on late-loaded online modules (RSK-13).
  - *4.5 CET / CFG Compatibility:* Documented RSK-12 in risk register; configured `/guard:cf` and `/CETCOMPAT:NO` to support dynamic x64 MinHook trampolines on Windows 11.
  - *4.6 Supply Chain Integrity:* Documented official Alexander Blade `dinput8.dll` download hygiene and SHA-256 verification instructions in `readme.md`.
  - *4.7 Config Robustness:* Verified strict 64 KiB cap, 500 lines, 1024 characters per line, `from_chars`, NaN/Inf rejection, and CRLF injection protection in `config.cpp`.
  - *4.8 Teardown Memory Safety:* Retained dead-by-design unhooking policy to eliminate multi-threaded crash hazards during process exit.
  - *4.9 ABI Static Assertions:* Added `static_assert(sizeof(MinimalVkPresentInfoKHR) == (sizeof(void*) == 8 ? 64 : 32))` and `static_assert(offsetof(MinimalVkPresentInfoKHR, pSwapchains) == (sizeof(void*) == 8 ? 40 : 20))`.
  - *4.10 Centralized Transfer Validation:* Implemented `RenderTargetManager::ValidateImageTransfer(srcMeta, dstMeta, isBlit)` centralizing usage flags, sample counts, dimensions, and depth/color format compatibility.

* **Full Vulkan Resource Pipeline:** **Present & Hardened** — Descriptor ping-ponging (C-02 Resolved), initial layout transitions & history clearing (C-03 Resolved), framebuffer/image-view tracking (`vkCreateImage`, `vkCreateImageView`, `vkCreateFramebuffer`, `VkRenderPassAttachmentBeginInfo`), and `CopyImageRegion` / `BlitToImage` are fully implemented.
  - *C-01 Resolved:* `quarterDepth` allocated as `VK_FORMAT_D32_SFLOAT`, gating `OnScenePassEnd`/`BlitToImage` on `colorCopied && depthCopied` to fail open safely if MSAA is disabled.
  - *C-02 Resolved:* Handled 2× MSAA passes without inline 1× resolve attachments via separate blit/resolve path.
  - *1× Single-Sample ASO Mode (Vega 7 APU Support):* Added `MsaaSamples` (`1` or `2`, default `2`) to `CBRConfig` and `cbr.ini`. Enables DDR4-bound integrated APUs running at 720p/1080p with in-game MSAA Off to bind `sampler2D` (`cbr_reconstruct_1x.spv`) and single-sample `quarterColor`/`quarterDepth`, eliminating 2× MSAA bandwidth overhead.
  - *5.1 Step 3 Blit Restore Transition:* Explicit `restoreDstStage = kVkPipelineStageColorAttachmentOutputBit` with `COLOR_ATTACHMENT_WRITE_BIT | COLOR_ATTACHMENT_READ_BIT` ensures subsequent UI/post passes observe complete blit image writes.
* **Scene Pass Identification (Section 5.2):** **Present & Hardened** — `Hooked_vkCreateRenderPass` and `Hooked_vkCreateRenderPass2`/`KHR` inspect attachment formats and sample counts (`RecordTrackedRenderPass`), and `Hooked_vkCmdEndRenderPass` gates on `rpCandidate && draws >= 250 && !colorIsSwapchain`. Verified depth `finalLayout == SHADER_READ_ONLY_OPTIMAL` and `config.forcedSceneRenderPassHandle` override.
* **DirectX 12 Parity (Section 5.3):** **Present & Hardened** — `CreateDX12Pipeline` (RootSignature + Compute PSO + runtime D3DCompile fallback + b1 camera CBV), `DispatchDX12` (resource barriers + descriptor table binding), and command-list VMT hooks (`ExecuteCommandLists`, `RSSetViewports`, `RSSetScissorRects`, `OMSetRenderTargets`, `DrawInstanced`, `DrawIndexedInstanced`) are implemented with official MSVC VMT indices (OMSetRenderTargets 46, RSSetViewports 21, ExecuteCommandLists 10).
  - *C-03 Resolved:* Saves and restores game descriptor heaps and compute PSO before and after CBR compute dispatch.
  - *H-02 Resolved:* Gated `OnScenePassEnd` and root signature/heap restoration behind `dx12InputsCaptured` safety check so D3D12 fails open cleanly without executing compute reconstruction over uninitialized buffers or corrupting root parameters.
  - *M-03 Resolved:* Fast-path guard in `Hooked_D3D12ExecuteCommandLists` bypasses redundant COM interface queries on the hot path.
  - *M-04 Resolved:* Secure `LoadLibraryExA(..., nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` locks DLL resolution for `d3d12.dll` and `d3dcompiler_47.dll` to System32.
* **Motion Vector Fallback & Matrix Gating (Section 5.4):** **Present & Guarded** — `CBRCameraUniforms` UBO (binding = 7 / register(b1)), `InvertMatrix4x4`, and `SetCameraMatrices` are implemented. Gated behind `cam.u_HasCameraMatrices != 0u` to prevent unvalidated ViewProj corruption.
  - *H-01 Resolved:* Removed duplicate `SetCameraMatrices` call in `CBREngine::OnBeginFrame()` and added floating-point difference check in `ReconstructionPass::SetCameraMatrices()` so intra-frame duplicate calls do not clobber `prevViewProj` to identity.
  - *M-02 Resolved:* Perspective cotangent bounds validation in `PatternScanner::GetCameraProjectionMatrix` ($0.2 \le \cot \le 5.0$) safely falls back to pure Strategy A viewport shift if camera scanning matches stale or dead memory.
* **In-Game ImGui Overlay & GPU Telemetry (Section 5.5):** **Decoupled API Contract** — Telemetry state management, hotkeys, and `GetFormattedTelemetry()` are present with `RegisterHudRenderCallback`; Dear ImGui draw backend remains planned for Phase 3.
  - *H-03 Resolved:* In `DispatchVulkan()`, prevented ~0.001 ms CPU time flicker when `vkGetQueryPoolResults` returns `VK_NOT_READY` by preserving the last valid GPU dispatch measurement. Added headerless `timestampPeriod` extraction from `VkPhysicalDeviceProperties` at offset 796 on 64-bit so GPU timing functions reliably across headerless and SDK builds.
* **Contrast Adaptive Sharpening (Section 5.6 & M-02):** **Present & Symmetrical** — Symmetric 5-tap RCAS with duplicate diagonal tap deduplicated (`(kCardinal[i].x, kCardinal[i].y != 0 ? kCardinal[i].y : 1)` mapping `i=2→(1,−1)` and `i=3→(1,1)`).
* **HDR / scRGB Dynamic Variance Floor (Section 5.7):** **Present & Scaled** — Dynamic std-dev floor scaling `max(rawStdDev, max(vec3(0.005), abs(mean) * 0.01))` across GLSL, Quad, and HLSL compute shaders, preserving HDR specular highlights.
* **Asynchronous Compute (Section 5.8):** **Intentionally Synchronous** — Kept on graphics queue to prevent cross-queue semaphore hazards on Pascal GP104.
* **Halton Jitter (Section 5.9):** **Intentionally Omitted** — Rejects Halton jitter with fallback warning to preserve 100% 4-quadrant checkerboard coverage.
* **Automated Headless Rendering Test (Section 5.10):** **3-Frame Multi-Frame Verification** — Expanded `tests/test_vulkan_headless.cpp` loop to 3 frames (`frame < 3`), verifying Frame 0 -> Frame 1 -> Frame 2 ping-pong feedback, layout transitions, and readback integrity on Lavapipe in CI.

## 8. Verification Checklist & Current Milestone Evaluation (Section 6)

### 8.1 Automated Test Suite & CI Baseline (100% Green)
The headless verification pipeline executes in GitHub Actions CI using headless software Vulkan (Mesa Lavapipe):
* **Execution Command:** `cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure`
* **Test Results (5/5 Passing, 0 Failed):**
  1. `cbr_core` — Unit tests covering configuration parsing, logger buffering, thread-safe access, jitter calculation, math invariants, and `ValidateImageTransfer`.
  2. `cbr_engine` — Lifecycle tests verifying engine initialization, swapchain transitions, graphics API detection, and VRAM footprint assertions (237.30 MiB at 4K).
  3. `cbr_engine_auto` — API autodetection tests selecting Vulkan or DX12 based on active module exports.
  4. `cbr_vulkan_headless` — Headless Vulkan compute execution allocating resources, binding descriptors, and dispatching 3 frames of ping-pong history feedback via Lavapipe.
  5. `cbr_shader_mapping` — Mathematical verification across 55,296 coordinate combinations, 100% 4-quadrant sample coverage, and 272 velocity shift/clamp boundary tests.
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

## 9. Readme Accuracy Audit & Claim Reconciliation (Section 7)

| Claim in `readme.md` | Audit Verdict | Codebase Implementation Evidence & Status |
| :--- | :--- | :--- |
| *"Live Vulkan graphics detours via MinHook (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, `vkCmdSetSampleLocationsEXT`)."* | **True** | Fully implemented in `src/hooks_vulkan.cpp` alongside 18 additional resource, image-view, framebuffer, physical device enumeration, and render pass tracking hooks. |
| *"Intercepts main scene color and depth attachments, routing them to quarter-resolution (1920×1080) targets configured with 2× MSAA."* | **True (Resolved in Codebase)** | • `res.quarterDepth` allocated as `VK_FORMAT_D32_SFLOAT` (126) with aspect mask `VK_IMAGE_ASPECT_DEPTH_BIT` (2u) (**C-01 Resolved**).<br>• `Hooked_vkCmdEndRenderPass` gates execution on `copiedColor && copiedDepth` to safely fail open if MSAA is disabled.<br>• 2× MSAA scene passes without inline 1× resolve attachments resolve/blit cleanly (**C-02 Resolved**).<br>• In-game MSAA required to be set to 2× (`VK_SAMPLE_COUNT_2_BIT`) as documented in Step 5.3. |
| *"Vulkan SPIR-V compute pipeline, 7-binding VkDescriptorSetLayout, VkDescriptorPool, VkDescriptorSet, and vkCmdPipelineBarrier memory barrier synchronization."* | **True (Upgraded to 8 bindings)** | `CreateVulkanPipeline` allocates an 8-binding descriptor layout (bindings 0–6 for MSAA inputs, history ping-pong, velocity, and storage output; binding 7 for `CBRCameraData` UBO) with layout transition barriers and ping-pong history descriptor updating. Documentation reconciled to 8 bindings. |
| *"Reconstruction pass writes a full-resolution history-depth target for the next frame's disocclusion test."* | **True** | Verified in `ReconstructionPass::UpdateVulkanPingPongDescriptors`: Set 0 writes `historyDepthViewB` and Set 1 writes `historyDepthViewA`. Shaders sample history depth via `texelFetch` / `.Load`. |
| *"In-game ImGui debug overlay (placeholder only; interactive F11/Insert key toggle in progress)."* | **True** | Accurately documented as a telemetry API with hotkey gating (`GetFormattedTelemetry()`, F11/Insert state toggles); Dear ImGui draw backend remains planned for Phase 3. |
| *"DirectX 12 graphics hooking parity (Vulkan primary; DX12 dummy device swapchain method planned for Phase 3)."* | **True (Hardened in Codebase)** | • VMT detours, root signature, compute PSO, and `DispatchDX12` implemented in `hooks_dx12.cpp` and `reconstruction_pass.cpp`.<br>• **C-03 Resolved:** Game command list descriptor heaps and compute PSO are safely saved and restored before and after CBR dispatch.<br>• **M-03 Resolved:** Fast-path check eliminates COM interface query overhead.<br>• **M-04 Resolved:** System32 DLL loading (`LOAD_LIBRARY_SEARCH_SYSTEM32`) prevents library planting. |
| *"Total CBR VRAM Footprint: 237.30 MiB (248.83 MB at 4K)."* | **True** | Verified in `RenderTargetManager::CalculateDimensions`: eliminated auxiliary full-res output color/depth storage buffers (Item M-06), reducing footprint from 300.59 MiB to 237.30 MiB (248.83 MB) at 4K and 59.33 MiB (62.21 MB) at 1080p. |
| *"Optional 5-tap RCAS contrast-adaptive sharpening (`SharpeningStrength = 0.0` default)."* | **True** | Fully implemented in all compute shaders (`cbr_reconstruct.comp`, `cbr_reconstruct_quad.comp`, `cbr_reconstruct.hlsl`) with deduplicated symmetric diagonal taps (Item M-02). |
| *"Offline-only single-player safety with anti-cheat module detection."* | **True** | `CheckAntiCheatModules()` inspects launch arguments at startup and periodically rescans loaded modules every 300 frames in `OnPostPresent` to disable CBR on online modules. |

---
*Report updated and validated for CODEBASE.md, Section 6 Verification Checklist, and Section 7 Readme Audit.*
