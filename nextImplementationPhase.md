# Next Implementation Phase: Runtime Graphics Detours & Frame Interception

**Project:** RDR2 Checkerboard Rendering Mod (CBR)  
**Target Platform:** Red Dead Redemption 2 (`1.0.1491.50`+) | PC (Windows 10/11 x64)  
**Target Hardware:** NVIDIA GeForce GTX 1070 Ti (Pascal GP104) & Vulkan / DirectX 12  
**Baseline Verification:** Passed in-game load test on 2026-10-23 (`rdr2-cbr.asi` at `0x00007FFE9ABA0000`, `vulkan-1.dll` at `0x00007FFE66510000`)

---

## 1. Executive Summary & Objective

In our initial in-game deployment, the mod successfully proved:
1. **Host Stability:** Loaded cleanly via Alexander Blade's ASI loader alongside Lenny's Mod Loader (`vfs.asi`) and Visual Redemption with **zero game crashes or memory corruption**.
2. **Subsystem Readiness:** Configuration loaded from [cbr.ini](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/cbr.ini), allocating 2× MSAA quarter-resolution buffers (1920×1080) with a measured 300.59 MiB VRAM footprint.
3. **Safe Fallback:** Because graphics hook stubs were protected by safety flags, the mod timed out gracefully after 60 seconds without disrupting game rendering.

### The Core Goal of This Phase
**Transition from dormant stub hooks to active function detours (via MinHook / API wrapping) that intercept RDR2's graphics pipeline, execute the checkerboard reconstruction compute pass, and blit the reconstructed 4K frame to the swapchain backbuffer prior to presentation.**

```
[RDR2 Render Thread]
        │
        ▼
   RAGE Engine (Draws to 1920x1080 2x MSAA Target + Jitter)
        │
        ▼
[vkQueuePresentKHR / IDXGISwapChain::Present Hooked]
        │
        ├──> 1. Acquire Current Backbuffer & Command Buffer
        ├──> 2. Insert Pipeline Barriers (Layout Transition)
        ├──> 3. Bind Shaders (cbr_reconstruct.comp) + Descriptors
        ├──> 4. Dispatch Compute Reconstruction (3840x2160 grid)
        ├──> 5. Blit / Resolve Reconstructed Image to Swapchain
        └──> 6. Call Original vkQueuePresentKHR / Present
```

---

## 2. Detailed Technical Breakdown & Tasks

### Task 2.1: MinHook Integration & Vulkan Detour Table

#### Problem Statement
Currently, [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp) locates `vulkan-1.dll` and resolves `vkGetInstanceProcAddr`, but returns `false` without modifying the dispatch table.

#### Implementation Steps
1. **Incorporate MinHook / Trampoline Library:**
   - Link `minhook` or vendor an in-tree x64 trampoline engine into [CMakeLists.txt](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/CMakeLists.txt).
   - Alternatively, intercept Vulkan via `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` hook chain, replacing function pointers in the device dispatch table before RAGE engine calls them.
2. **Detour Targets:**
   - `vkCreateSwapchainKHR`: Intercept swapchain creation to capture the `VkDevice`, backbuffer count, extent (width/height), and format (`VK_FORMAT_B8G8R8A8_UNORM` / `VK_FORMAT_R16G16B16A16_SFLOAT`).
   - `vkDestroySwapchainKHR`: Release allocated compute pipelines, command buffers, and descriptor sets associated with the stale swapchain.
   - `vkQueuePresentKHR`: Intercept every frame right before presentation to execute the CBR reconstruction pass.
   - `vkAcquireNextImageKHR`: Track active backbuffer index.

```cpp
// Target MinHook Hook Structure for Vulkan
MH_STATUS status = MH_CreateHook(
    reinterpret_cast<LPVOID>(pfn_vkQueuePresentKHR),
    reinterpret_cast<LPVOID>(&Hooked_vkQueuePresentKHR),
    reinterpret_cast<LPVOID*>(&g_Original_vkQueuePresentKHR)
);
MH_EnableHook(pfn_vkQueuePresentKHR);
```

---

### Task 2.2: Swapchain Image Acquisition & Command Injection

#### Implementation Steps
1. **Swapchain Resource Tracking:**
   - Enumerate swapchain images via `vkGetSwapchainImagesKHR`.
   - Create a `VkImageView` for each swapchain image for compute write or blit destinations.
2. **Dedicated Command Infrastructure:**
   - Create a dedicated `VkCommandPool` associated with the presentation queue family (`VkQueueFamilyProperties`).
   - Allocate a `VkCommandBuffer` and `VkFence` per swapchain image to ensure non-blocking, multi-frame synchronization without stalling the GPU pipeline.
3. **Pipeline Barriers & Synchronization:**
   - Insert execution barrier transitioning the swapchain backbuffer from `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` to `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` (or `VK_IMAGE_LAYOUT_GENERAL`).
   - Synchronize with the game's render completion semaphore (`waitSemaphoreCount` in `VkPresentInfoKHR`).

---

### Task 2.3: Reconstruction Compute Pass Execution

#### Implementation Steps
1. **SPIR-V Shader Module Management:**
   - Ensure [shaders/cbr_reconstruct.comp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/shaders/cbr_reconstruct.comp) is compiled to SPIR-V bytecode (`cbr_reconstruct.spv`) during build time via `glslangValidator -V`.
   - Embed or load the binary at runtime via `vkCreateShaderModule`.
2. **Descriptor Set Layout:**
   - Binding 0: `QuarterColorMSAA` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`)
   - Binding 1: `QuarterDepthMSAA` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`)
   - Binding 2: `HistoryColor` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`)
   - Binding 3: `VelocityVectors` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`)
   - Binding 4: `ReconstructedOutput` (`VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`)
   - Binding 5: Push Constants (Dimensions, Parity, Jitter Offsets, Camera Near/Far).
3. **Dispatch Dimensions:**
   - Dispatch workgroups covering the 3840×2160 canvas:
     $$\text{Groups}_X = \lceil 3840 / 16 \rceil = 240, \quad \text{Groups}_Y = \lceil 2160 / 16 \rceil = 135$$
4. **Blit / Copy to Swapchain:**
   - Use `vkCmdBlitImage` or direct compute storage write to place the final reconstructed 4K image onto the swapchain backbuffer.
   - Transition swapchain image back to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` prior to calling original `vkQueuePresentKHR`.

---

### Task 2.4: Camera Projection Matrix & Subpixel Jitter Injection

#### Problem Statement
For checkerboard reconstruction to resolve clean geometry, alternating pixels must be sampled across successive frames.

#### Implementation Steps
1. **Memory Pattern Scanning for RDR2 `1.0.1491.50`:**
   - Scan RAGE camera manager memory space for projection matrix updates.
   - Inject alternating horizontal/vertical subpixel jitter offsets:
     * Frame $N$: $(+0.25 / W, +0.25 / H)$
     * Frame $N+1$: $(-0.25 / W, -0.25 / H)$
2. **Programmable Sample Locations (Vulkan Alternative):**
   - If camera matrix patching is bypassed, utilize `VK_EXT_sample_locations` on the 2× MSAA render target, toggling sample locations between frames according to [CHECKERBOARD.md](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/CHECKERBOARD.md):
     * Frame 0: $(0.75, 0.75)$ and $(0.25, 0.25)$
     * Frame 1: $(0.25, 0.75)$ and $(0.75, 0.25)$

---

### Task 2.5: DirectX 12 Parity Hooking

#### Implementation Steps
1. **Dummy Device Swapchain Method:**
   - In [src/hooks_dx12.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_dx12.cpp), spawn a lightweight hidden window and create a temporary `D3D12CreateDevice` and `IDXGIFactory4::CreateSwapChain`.
   - Query the Virtual Method Table (VMT) of `IDXGISwapChain3` to retrieve the address of `Present` (VMT index 8).
   - Hook `Present` via MinHook or VMT pointer swap.
2. **DX12 Pipeline Execution:**
   - Record compute command list executing [shaders/cbr_reconstruct.hlsl](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/shaders/cbr_reconstruct.hlsl) on `ID3D12CommandQueue`.

---

## 3. Implementation Roadmap & Timeline

| Phase Subtask | Description | Status | Target Outcome |
|---|---|---|---|
| **Phase 2.1** | MinHook Integration & Vulkan Proc Table Detours | **COMPLETE** | Intercept `vkQueuePresentKHR` & swapchain events reliably |
| **Phase 2.2** | Swapchain Image Tracking & Hook Wiring | **COMPLETE** | Intercept `vkCreateSwapchainKHR`, wire real `VkDevice`, track extent & recreation |
| **Phase 2.3** | SPIR-V Loader & Compute Pipeline Dispatch | **COMPLETE** | Robust SPIR-V loader, `VkComputePipeline` creation, push constants (80B), and `vkCmdDispatch` |
| **Phase 2.4** | Render Target Hooking & Resolution Redirection | **COMPLETE** | Intercept `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdEndRenderPass`, scale down to 1080p 2× MSAA |
| **Phase 2.5** | Camera Jitter Injection / Sample Locations | **COMPLETE** | Strategy A (0.5px viewport jitter) & Strategy B (`VK_EXT_sample_locations`) + AOB camera pattern scanner |
| **Phase 2.6** | Telemetry, UI Overlay & Performance Tuning | **IN PROGRESS** | Real-time FPS, frame-time graphs & interactive ImGui toggles |

---

## 4. Verification & Validation Criteria

1. **Safety First:** Mod must never crash RDR2 or throw Vulkan validation errors. If any device call fails, the mod must fall back to the original `vkQueuePresentKHR` immediately.
2. **Visual Fidelity:** Text, HUD, and minimap must remain razor sharp without jitter or blur.
3. **Performance Target:** Overall reconstruction compute pass frame time must execute in **< 2.0 ms** on GTX 1070 Ti at 4K UHD.

---

## 5. RAGE Engine (RDR2) Runtime Rendering Gotchas & Engineering Countermeasures

### 5.1 Dithered Effects & Temporal Accumulation (TAA Interplay)
* **The Gotcha:** RDR2 uses stippled, dithered subpixel patterns for Screen-Space Ambient Occlusion (SSAO), soft contact shadows, character hair/beard alpha cards, and foliage. If in-game TAA is turned Off, the RAGE engine ceases generating its primary velocity buffer and exposes raw dither dots. Conversely, if both in-game TAA and CBR's `HistoryWeight = 0.90` run back-to-back, temporal history becomes double-damped, resulting in excessive blurring and ghost trails on foliage and horse manes.
* **Countermeasure:**
  1. Default `HistoryWeight` is adjusted to **`0.85`** in [cbr.ini](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/cbr.ini) (with a recommended range of `0.75`–`0.85` when in-game TAA is active).
  2. Subpixel camera projection jitter is decoupled from the game's Halton sequence (see Section 5.5), ensuring that RDR2's native TAA and CBR operate on aligned phase offsets.

### 5.2 Opaque Deferred vs. Forward Translucent Render Passes
* **The Gotcha:** RDR2 separates its rendering pipeline into:
  1. Deferred G-buffer pass (opaque geometry, terrain, structures)
  2. Async compute passes (volumetric clouds, fog, atmospheric scattering)
  3. Clustered deferred lighting passes
  4. Forward translucent pass (glass, water surfaces, alpha-blended particles)
  5. Post-processing (bloom, tonemapping, depth of field) and UI compositing.
  If CBR downscales the early G-buffer pass and immediately resolves/blits mid-frame on `vkCmdEndRenderPass`, downstream forward lighting or translucent passes may receive mismatched resolutions or operate without reconstruction.
* **Countermeasure:**
  In [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp), render pass tracking filters for HDR color + depth attachment candidates (`rpCandidateGeometryPass`) and enforces a threshold of $\ge 250$ draw calls. All sub-passes sharing G-buffer attachments are downscaled consistently, and `OnScenePassEnd` executes exactly once per frame (`m_lastDispatchedFrame.exchange(currentFrame)`) right before post-processing and UI passes begin.

### 5.3 Camera Cutscenes, Scope Zooms, and Lighting Transitions
* **The Gotcha:** During camera angle transitions in cutscenes, rapid sniper scope zooming, or transitions between interior and exterior spaces, $100\%$ of the temporal history buffer becomes invalid in a single frame. Previously, `ResetHistory()` merely flipped the ping-pong index to `0u` without forcing `u_FrameIndex = 0` in push constants, causing the shader to blend up to $90\%$ of the stale camera frame for 1 to 2 frames.
* **Countermeasure:**
  1. Added an atomic `m_forceHistoryReset` flag in [include/cbr/render_target_manager.h](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/include/cbr/render_target_manager.h).
  2. `ResetHistory()` and `ForceHistoryReset()` arm `m_forceHistoryReset.store(true)`.
  3. In [src/reconstruction_pass.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/reconstruction_pass.cpp), `BuildReconstructionPushConstants()` consumes the flag via `ConsumeForceHistoryReset()` and forces `pc.frameIndex = 0u` for that single dispatch. In both GLSL and HLSL compute shaders, `frameIndex == 0u` marks the entire screen as `isDisoccluded = true`, immediately flushing stale history.
  4. In `SetCameraMatrices()`, cumulative matrix deltas exceeding a cut threshold ($\Delta > 15.0$) automatically invoke `ForceHistoryReset()`.

### 5.4 Dynamic Resolution & Windowed Borderless Alt-Tabbing
* **The Gotcha:** Pressing Alt+Enter, Alt+Tab, or changing monitor displays prompts RDR2 to recreate swapchains or resize render extents. While `vkDeviceWaitIdle` prevents in-flight command crashes, internal tracking maps (`g_images`, `g_imageViews`, `g_framebuffers`, `g_renderPasses`) in [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp) can retain stale pointers that collide with recycled driver handles.
* **Countermeasure:**
  Inside `Hooked_vkCreateDevice` and `Hooked_vkDestroySwapchainKHR`, `g_attachmentMutex` is locked and all tracking maps (`g_images`, `g_imageViews`, `g_framebuffers`, `g_renderPasses`) are explicitly cleared, guaranteeing that recycled Vulkan handles never inherit stale metadata across swapchain lifecycles.

### 5.5 Overwriting Halton Subpixel Jitter to Prevent Double-Jittering
* **The Gotcha:** When in-game TAA is active, RDR2 continuously injects a Halton subpixel jitter sequence into elements `proj[8]` and `proj[9]` of its camera projection matrix. In Strategy A, adding CBR's $1.0\text{ px}$ subpixel shift on top of an unknown Halton offset shifts rasterization off the exact $(0.25, 0.25)$ and $(0.75, 0.75)$ diagonal sample centers required by `MapPixelToSample`.
* **Countermeasure:**
  In [src/jitter_manager.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/jitter_manager.cpp) (`ApplyJitterToProjection` and `SetProjectionJitter`), the base offset for symmetric camera frustums ($|\text{cur8}| < 0.015\text{ and }|\text{cur9}| < 0.015$) is clamped to `0.0f`. This overwrites RDR2's native Halton jitter with the exact, deterministic CBR checkerboard subpixel offset, ensuring $100\%$ sample alignment without double-jitter artifacts.

