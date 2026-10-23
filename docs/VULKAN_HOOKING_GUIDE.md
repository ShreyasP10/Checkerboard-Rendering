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

### Step 3.2: Frame Interception & Reconstruction (`vkQueuePresentKHR`)

Every rendered frame passes through `vkQueuePresentKHR` before being displayed on screen:

```cpp
int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return -3; // VK_ERROR_INITIALIZATION_FAILED
    }

    const auto* presentInfo = static_cast<const VkPresentInfoKHR*>(pPresentInfo);

    // 1. Execute Checkerboard Reconstruction Compute Pass
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

## 4. Vulkan Synchronization & Barrier Management

Before reading from RDR2's render targets and writing to the swapchain backbuffer, explicit memory barriers are mandatory:

```
[RDR2 Render Pass Completes]
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
   DstStage: VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
   AccessMask: VK_ACCESS_SHADER_WRITE_BIT -> VK_ACCESS_MEMORY_READ_BIT
   Layout: VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
          │
          ▼
 [vkQueuePresentKHR Presentation]
```

---

## 5. Fallback & Anti-Crash Invariants

1. **Loader Lock Isolation:** Hook installation must never execute inside `DllMain`. It must run on the dedicated `CBRInitThread`.
2. **Device Lost Handling:** If `VK_ERROR_DEVICE_LOST` or `VK_SUBOPTIMAL_KHR` occurs, tear down intermediate buffers immediately and defer reconstruction until the swapchain is re-established.
3. **Strict Exception Suppression:** Graphics APIs expect pure C calling conventions (`VKAPI_CALL`). Never leak a C++ exception into RDR2's render thread.
