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

### 5.6 Three Downstream Depth Downsample Passes (SSAO, SSR, and Volumetrics)
* **The Gotcha:** After the main geometry pass finishes, RDR2 executes three separate depth-downsample passes in a single frame: one for Screen-Space Ambient Occlusion (SSAO), one for Screen-Space Reflections (SSR) on water and wet mud, and one for volumetric fog and light-shaft occlusion. If viewport downscaling occurs during the early depth pre-pass (Z-prepass), the game's full-resolution depth buffer only contains valid depth in the top-left quarter quadrant, causing SSAO, water reflections, and sunbeams to either disappear or align to a miniaturized quarter-sized ghost.
* **Countermeasure:**
  In [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp), `rp.isCandidateGeometryPass` requires both `hasHdrColor && hasDepth`. Because the early Z-prepass contains only depth attachments and no color attachments, it is automatically excluded from viewport downscaling and executes at full resolution ($1920\times 1080$ or native swapchain extent). Downstream SSAO, SSR, and Volumetrics read this pristine, full-resolution depth buffer while only the heavy PBR shading passes run at quarter resolution.

### 5.7 B10G11R11_UFLOAT_PACK32 HDR Format vs. R16G16B16A16_SFLOAT Copy Check
* **The Gotcha:** RDR2 heavily utilizes `VK_FORMAT_B10G11R11_UFLOAT_PACK32` (format 122, 32-bit / 4 bytes per pixel) for intermediate lighting and bloom targets to save memory bandwidth. Vulkan `vkCmdCopyImage` strictly mandates identical texel block sizes (`VUID-vkCmdCopyImage-srcImage-01728`), making raw copies between 32-bit B10G11R11 and 64-bit R16G16B16A16_SFLOAT invalid.
* **Countermeasure:**
  In [src/render_target_manager.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/render_target_manager.cpp), `CanCopyToQuarterColor()` explicitly permits format 122 when operating in 1× APU single-sample mode (`msaaSamples == 1`). In [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp), `Hooked_vkCmdEndRenderPass` automatically branches to `ReconstructionPass::BlitToImage()` using hardware filtering (`VK_FILTER_NEAREST`) to convert B10G11R11 into R16G16B16A16_SFLOAT seamlessly without Vulkan validation errors.

### 5.8 Stencil-Masked Local Lighting & Double-Drawn Forward Translucency
* **The Gotcha:** RDR2 splits lighting into Global Light (sun/moon) and Local Light (lanterns, campfires), using hardware stencil bits (`D32_SFLOAT_S8_UINT`) to mask translucent surfaces. Following deferred lighting, Forward Shading draws translucent surfaces back-to-front twice (back-faces then front-faces). If viewport downscaling desynchronizes between deferred lighting and forward translucency, stencil masks and depth tests mismatch by $2\times$.
* **Countermeasure:**
  Viewport downscaling in [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp) remains active across both deferred lighting and forward translucency passes that share the scene framebuffer attachments, and `OnScenePassEnd` is triggered exactly once right before the multi-mip Bloom downsample chain begins.

### 5.9 Negative MIP LOD Bias (-1.0f) Corrupting Pre-Filtered IBL Cubemaps & Bloom
* **The Gotcha:** RDR2 convolves dynamic environment cubemap mips for Split-Sum Image-Based Lighting (IBL), where mip level encodes surface roughness (mip 0 = mirror chrome, mip 4 = rough mud/cloth). Applying `-1.0f` MIP LOD bias to all anisotropic samplers causes rough materials to sample glossy, mirror-like reflections, making muddy coats and horse fur appear artificially wet or metallic.
* **Countermeasure:**
  In [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp) (`Hooked_vkCreateSampler`), the sampler modification requires:
  `pCreateInfo->addressModeU == kVkSamplerAddressModeRepeat && pCreateInfo->addressModeV == kVkSamplerAddressModeRepeat`.
  Because geometry and terrain material textures tile with `VK_SAMPLER_ADDRESS_MODE_REPEAT` (0), while IBL cubemaps, LUTs, and bloom chains use `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` (2), this strictly confines negative LOD bias to tiling geometry textures, completely safeguarding IBL cubemaps and postfx chains.

### 5.10 Planar Mirror Reflections Running Full Deferred Passes
* **The Gotcha:** When Arthur enters saloons, hotel rooms, or tailor shops with planar mirrors, RDR2 executes a secondary deferred rendering pass from the mirror's reflected frustum. If this pass draws $\ge 250$ draw calls before the main scene pass, `Hooked_vkCmdEndRenderPass` could mistake it for the primary scene pass and corrupt temporal history with the inverted mirror view.
* **Countermeasure:**
  In [src/hooks_vulkan.cpp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/src/hooks_vulkan.cpp), `Hooked_vkCmdEndRenderPass` strictly enforces `extentMatchesMainScene`:
  `(rw == sceneDims.fullWidth && rh == sceneDims.fullHeight) || (downscaled && rw == sceneDims.quarterWidth && rh == sceneDims.quarterHeight)`.
  Planar mirrors and cubemaps render to square/off-aspect render targets (e.g. 512×512, 1024×1024) and are rejected unconditionally, preserving the primary camera view.

### 5.11 Quarter-Resolution Raymarched Volumetric Clouds & Fog Aliasing
* **The Gotcha:** RDR2 already evaluates raymarched volumetric clouds and height fog at quarter resolution, accumulating them temporally. Applying whole-frustum camera projection matrix jitter on odd frames induces "screen-door" checkerboard pixelation on clouds, hair, and horse tails.
* **Countermeasure:**
  On APUs and integrated graphics, Strategy A uses subpixel Viewport Shift ($0.5\text{ px}$ horizontal shift on the geometry pass only) rather than global projection matrix perturbation, preventing CBR's 2-phase jitter from polluting the engine's compute-based volumetric cloud raymarcher.

### 5.12 Hardware Tessellation (TS Stage) on Snow and Mud Displacement Maps
* **The Gotcha:** RDR2 writes dynamic deformation (footprints, wagon ruts) to a 2048×2048 displacement map for Parallax Occlusion Mapping and Hardware Tessellation. The hull shader computes subdivision factors from screen-space edge lengths; halving the viewport reduces tessellation density by $50\%$.
* **Impact & Guidance:**
  On the AMD Radeon Vega 7 APU, this density reduction is a major performance benefit, alleviating GCN 5.0 tessellation engine bottlenecks in Valentine and Grizzlies West. On discrete GPUs (GTX 1070 Ti), setting in-game *Tessellation Quality* to **High** or **Ultra** restores full polygon density.

### 5.13 Aggressive G-Buffer Clears & Unwritten Bottom-Right 75% Quadrant Bleed
* **The Gotcha:** RDR2 clears G-buffer targets at the start of each pass (`VK_ATTACHMENT_LOAD_OP_CLEAR`). Rendering only into $[0..\text{quarterWidth}, 0..\text{quarterHeight}]$ leaves the remaining $75\%$ filled with cleared black texels `(0, 0, 0, 0)`. Any linear sampler reading `gameColorImage` directly would bleed black texels into the right and bottom boundaries.
* **Countermeasure:**
  `Hooked_vkCmdEndRenderPass` copies strictly the $[0..\text{quarterWidth}, 0..\text{quarterHeight}]$ subregion into a tightly-sized `res.quarterColor` image using `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` (2). The compute reconstruction shader operates exclusively on the tightly bound scratch buffers and never samples unwritten regions.

### 5.14 Async Compute (`system.xml`) Queue Family Ownership & Pipeline Switches
* **The Gotcha:** When `<asyncComputeEnabled value="true" />` is configured in `system.xml`, RDR2 dispatches volumetric compute tasks to an asynchronous compute queue family. Accessing these attachments from the graphics queue without cross-queue ownership transfers creates synchronization hazards.
* **Guidance:**
  Configuring `<asyncComputeEnabled value="false" />` in `system.xml` is documented and recommended on Pascal GPUs (which lack hardware async compute scheduling) and when using the CBR mod, ensuring all scene attachments remain coherent on the primary graphics queue family.

### 5.15 Weapon Wheel, Pause Map & Photo Mode Shimmer Elimination
* **The Gotcha:** Pausing the game or opening the Weapon Wheel freezes the 3D scene while presenting frames at 60 Hz. Alternating even/odd subpixel offsets on completely static geometry can produce subtle 30 Hz edge shimmering.
* **Countermeasure:**
  In [shaders/cbr_reconstruct.comp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/shaders/cbr_reconstruct.comp), [shaders/cbr_reconstruct_quad.comp](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/shaders/cbr_reconstruct_quad.comp), and [shaders/cbr_reconstruct.hlsl](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/shaders/cbr_reconstruct.hlsl), the active pixel reconstruction branch checks `isStatic`:
  `float effectiveWeight = isStatic ? max(pc.u_HistoryWeight, 0.96) : pc.u_HistoryWeight;`
  `finalColor = mix(currentColor, historyColor.rgb, effectiveWeight);`
  Boosting temporal blend weight to $0.96$ on static frames completely damps 30 Hz parity shimmer to imperceptible levels.


