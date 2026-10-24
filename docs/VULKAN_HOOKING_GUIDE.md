# Vulkan Hooking & Pipeline Interception Guide for RDR2

**Project:** RDR2 Checkerboard Rendering Mod (CBR)  
**Target API:** Vulkan 1.2 / 1.3 | Windows x64  
**Target Engine:** Rockstar Advanced Game Engine (RAGE)  
**Maintainer:** Shreyas Pawar  

---

## 1. Overview & Architecture

Vulkan uses an Explicit Multi-Layer Loader Architecture. Unlike DirectX, where interfaces expose virtual method tables (VMT), Vulkan dispatching involves:
1. **Loader (`vulkan-1.dll`):** Dispatches calls from the application to the appropriate Installable Client Driver (ICD), such as NVIDIA's `nvoglv64.dll`.
2. **Device & Instance Dispatch Tables:** The application queries function pointers using `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`.

```
[RDR2 Executable]
       │
       ▼ calls vkQueuePresentKHR
[vulkan-1.dll Export / Trampoline]  <─── Injected MinHook Detour (Hooked_vkQueuePresentKHR)
       │
       ▼ (if unhooked)
[NVIDIA ICD (nvoglv64.dll)]
```

---

## 2. Hooking Techniques: Evaluation & Selection

| Method | Mechanism | Stability in RDR2 | Complexity | Recommendation |
|---|---|---|---|---|
| **A. Export Detour (MinHook)** | Inline detour of exported functions in `vulkan-1.dll` (`vkQueuePresentKHR`, `vkCreateSwapchainKHR`) | ⭐⭐⭐⭐⭐ Highest | Low-Medium | **Primary (Recommended)** |
| **B. GetProcAddr Interception** | Hook `vkGetInstanceProcAddr` & `vkGetDeviceProcAddr` to return custom wrapper function pointers | ⭐⭐⭐⭐ High | Medium | **Secondary / Fallback** |
| **C. Implicit Vulkan Layer** | Register custom JSON manifest layer in Windows registry (`VK_LAYER_PATH`) | ⭐⭐⭐ Medium | High | Overkill for ASI mod |

### Selected Strategy: Export Detour with MinHook
Because `vulkan-1.dll` is directly loaded by RDR2 (`0x00007FFE66510000`), placing inline detours on the exported `vkQueuePresentKHR` and `vkCreateSwapchainKHR` symbols guarantees capture of all presentation and swapchain lifecycle calls regardless of how RAGE engine cached its function pointers.

---

## 3. Step-by-Step Implementation

### Step 3.1: Intercepting Swapchain Creation (`vkCreateSwapchainKHR`)

When RDR2 creates or resizes its window / swapchain, `vkCreateSwapchainKHR` is invoked.

```cpp
int Hooked_vkCreateSwapchainKHR(
    void* device,
    const void* pCreateInfo,
    const void* pAllocator,
    void* pSwapchain)
{
    // 1. Call original function first to obtain valid VkSwapchainKHR
    int result = g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (result != 0) { // VK_SUCCESS
        return result;
    }

    // 2. Extract swapchain dimensions and format
    const auto* createInfo = static_cast<const VkSwapchainCreateInfoKHR*>(pCreateInfo);
    uint32_t width  = createInfo->imageExtent.width;
    uint32_t height = createInfo->imageExtent.height;
    VkFormat format = createInfo->imageFormat;

    // 3. Initialize CBR Vulkan device resources
    CBREngine::Get().OnSwapchainRecreated(width, height);
    ReconstructionPass::Get().InitializeVulkan(device, *static_cast<VkSwapchainKHR*>(pSwapchain));

    return result;
}
```

---

### Step 3.2: Frame Interception & Presentation (`vkQueuePresentKHR`)

Every rendered frame passes through `vkQueuePresentKHR` before being displayed on screen:

```cpp
int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return -3; // VK_ERROR_INITIALIZATION_FAILED
    }

    const auto* presentInfo = static_cast<const VkPresentInfoKHR*>(pPresentInfo);

    // 1. Notify engine frame lifecycle
    try {
        CBREngine::Get().OnPrePresent(queue, presentInfo);
    } catch (...) {
        CBR_LOG_ERROR("Exception caught during CBR OnPrePresent; proceeding to native present.");
    }

    // 2. Call original presentation
    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent(queue);
    } catch (...) {
    }

    return result;
}
```

---

### Step 3.3: Dynamic Viewport & Scissor Downscaling (`vkCmdSetViewport`, `vkCmdSetScissor`)

To achieve quarter-resolution rendering without altering RDR2's window or swapchain resolution, CBR intercepts `vkCmdSetViewport` and `vkCmdSetScissor`.

1. **Geometry Pass Detection:** When RDR2 sets a full-resolution viewport (e.g. 3840×2160), CBR identifies it as a candidate 3D scene pass and downscales it to 1080p (1920×1080).
2. **Strategy A (Viewport Shift Jitter):** For `SamplingStrategy::StrategyA`, CBR applies a ±0.5px horizontal subpixel offset on alternate frames:
   ```cpp
   if (cfg.samplingStrategy == SamplingStrategy::StrategyA) {
       float jitterOffset = (g_frameIndex % 2 == 1) ? 0.5f : -0.5f;
       scaledViewport.x += jitterOffset;
   }
   ```
3. **Strategy B (Stationary Viewport):** For `SamplingStrategy::StrategyB`, the viewport is kept stationary at integer pixel coordinates, deferring subpixel sampling to `vkCmdSetSampleLocationsEXT`.
4. **UI/HUD Pass Passthrough:** Viewports smaller than full resolution (or flagged as UI overlays) pass through unmodified to guarantee 100% native clarity for the minimap and menus.

---

### Step 3.4: Mid-Frame Interception & Scene Pass Completion (`vkCmdEndRenderPass`)

Executing reconstruction compute passes during `vkQueuePresentKHR` can lead to artifacts if post-processing or UI compositing has already completed. CBR detours `vkCmdBeginRenderPass` and `vkCmdEndRenderPass`:

1. **Scene Pass Tracking:** `Hooked_vkCmdBeginRenderPass` monitors the active render pass.
2. **Reconstruction Trigger:** When the primary HDR geometry render pass finishes in `Hooked_vkCmdEndRenderPass`, CBR dispatches `cbr_reconstruct.comp` before the post-processing and UI passes begin.
3. **Pipeline Barriers:** Inserts `VkMemoryBarrier` and `VkImageMemoryBarrier` transitions from `VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT` to `VK_ACCESS_SHADER_READ_BIT`.

---

### Step 3.5: Programmable Sample Locations (`vkCmdSetSampleLocationsEXT`)

When `SamplingStrategy = StrategyB` is configured in `cbr.ini`, CBR utilizes the `VK_EXT_sample_locations` extension:

1. **2× MSAA Grid Alternation:**
   - **Even Frames (Parity 0):** Sample 0 at $(0.75, 0.75)$, Sample 1 at $(0.25, 0.25)$.
   - **Odd Frames (Parity 1):** Sample 0 at $(0.25, 0.75)$, Sample 1 at $(0.75, 0.25)$.
2. **Mutual Exclusivity Enforced:** As established in `CHECKERBOARD.md`, viewport jitter is disabled when Strategy B is active to avoid sample cancellation.

---

## 4. Vulkan Descriptor Pooling & Barrier Management

In `reconstruction_pass.cpp`, CBR manages compute pipeline resources dynamically:

### Descriptor Set Layout (Bindings 0–6)
* Binding 0: `s_CurrentColorMS` (2× MSAA sampler2DMS)
* Binding 1: `s_CurrentDepthMS` (2× MSAA sampler2DMS)
* Binding 2: `s_HistoryColor` (sampler2D, Linear Clamp)
* Binding 3: `s_HistoryDepth` (sampler2D, Point Clamp)
* Binding 4: `s_Velocity` (sampler2D, Linear Clamp)
* Binding 5: `u_ReconstructedColor` (image2D, write-only storage)
* Binding 6: `u_ReconstructedDepth` (image2D, write-only storage)

### Execution Barriers
```
[RDR2 Geometry Pass Completes]
          │
          ▼
 Pipeline Barrier:
   SrcStage: VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   DstStage: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
   AccessMask: VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT -> VK_ACCESS_SHADER_READ_BIT
          │
          ▼
 [Dispatch Compute Shader: cbr_reconstruct.comp]
          │
          ▼
 Pipeline Barrier:
   SrcStage: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
   DstStage: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   AccessMask: VK_ACCESS_SHADER_WRITE_BIT -> VK_ACCESS_SHADER_READ_BIT
          │
          ▼
 [RDR2 Post-Processing & UI Compositing]
          │
          ▼
 [vkQueuePresentKHR Presentation]
```

---

## 5. Fallback & Anti-Crash Invariants

1. **Loader Lock Isolation:** Hook installation must never execute inside `DllMain`. It must run on the dedicated `CBRInitThread`.
2. **Device Lost Handling:** If `VK_ERROR_DEVICE_LOST` or `VK_SUBOPTIMAL_KHR` occurs, tear down intermediate buffers immediately and defer reconstruction until the swapchain is re-established.
3. **SEH Safe Memory Scanning:** Memory signature scans for camera matrices use structured exception handling and `VirtualQuery` bounds verification to prevent access violations.
4. **Strict Exception Suppression:** Graphics APIs expect pure C calling conventions (`VKAPI_CALL`). Never leak a C++ exception into RDR2's render thread.

