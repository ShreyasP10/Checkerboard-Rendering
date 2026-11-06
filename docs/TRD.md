# Technical Requirements Document (TRD)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** TRD-RDR2CBR-001  
**Version:** 1.0  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. System Architecture & Component Breakdown

The architecture is composed of six modular subsystems:

```
+-------------------------------------------------------------------------+
|                              cbr_engine                                 |
|                                                                         |
|  +--------------------+   +-----------------------+   +--------------+  |
|  |    HookManager     |   | RenderTargetManager   |   | JitterManager|  |
|  | - Vulkan Hooks     |   | - 2x MSAA Targets     |   | - Matrix Calc|  |
|  | - DX12 Hooks       |   | - History Ping-Pong   |   | - Uniform Inj|  |
|  | - MinHook Core     |   | - VRAM Allocator      |   |              |  |
|  +---------+----------+   +-----------+-----------+   +-------+------+  |
|            |                          |                       |         |
|            +--------------------------+-----------------------+         |
|                                       |                                 |
|                                       v                                 |
|                     +-----------------------------------+               |
|                     |        ReconstructionPass         |               |
|                     | - Pipeline & Descriptor Sets      |               |
|                     | - Compute Shader Dispatch         |               |
|                     | - Barrier Synchronization         |               |
|                     +-----------------+-----------------+               |
|                                       |                                 |
|                                       v                                 |
|                     +-----------------------------------+               |
|                     |         UIOverlay & Config        |               |
|                     | - ImGui Overlay                   |               |
|                     | - cbr.ini Parser / Logger         |               |
|                     +-----------------------------------+               |
+-------------------------------------------------------------------------+
```

---

## 2. API Interception & Hooking Strategy

For comprehensive step-by-step guidance, refer to:
* **Engineering Master Plan:** [nextImplementationPhase.md](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/nextImplementationPhase.md)
* **Vulkan Technical Guide:** [docs/VULKAN_HOOKING_GUIDE.md](file:///c:/Users/Dinesh%20B%20Pawar/OneDrive/Desktop/Checkerboard-Rendering/docs/VULKAN_HOOKING_GUIDE.md)

### 2.1 Vulkan Interception Architecture
The mod intercepts the Vulkan runtime via `vulkan-1.dll` (located at `0x00007FFE66510000` in RDR2 build `1.0.1491.50`):
1. **Dynamic DLL Hooking via MinHook:** Intercepts exported symbols from `vulkan-1.dll`:
   - `vkQueuePresentKHR`: Coordinates frame lifecycle, tracking presentation intervals and frame index.
   - `vkCreateSwapchainKHR` & `vkDestroySwapchainKHR`: Captures backbuffer resolution, format, and swapchain recreation events.
   - `vkAcquireNextImageKHR`: Tracks the active presentation backbuffer index.
   - `vkCmdSetViewport` & `vkCmdSetScissor`: Intercepts 4K geometry passes, downscaling them to 1080p 2× MSAA while keeping UI/HUD overlays at native 4K.
   - `vkCmdBeginRenderPass` & `vkCmdEndRenderPass`: Detects scene geometry pass completion mid-frame to execute the SPIR-V reconstruction compute pass prior to post-processing and UI compositing.
   - `vkCmdSetSampleLocationsEXT`: Toggles hardware subpixel sample locations on alternate frames when Strategy B is active.
2. **Hook Table Interception:** Wraps the device dispatch table pointers:
   - `vkCreateDevice`
   - `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`
   - `vkCmdDraw`, `vkCmdDrawIndexed`
   - `vkCreateImage`, `vkCreateSampler`

```cpp
// Hook prototype for vkQueuePresentKHR with safe fallback
int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) return -3; // VK_ERROR_INITIALIZATION_FAILED

    try {
        CBREngine::Get().OnPrePresent(queue, pPresentInfo);
    } catch (...) {
        // Exception suppression: never let C++ exceptions spill into RDR2 render thread
    }

    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent(queue);
    } catch (...) {}

    return result;
}
```

### 2.2 Sampling Strategy Differentiation
To prevent destructive sample cancellation, the mod strictly isolates two mutually exclusive sampling mechanisms:
1. **Strategy A (Viewport Shift Jitter):**
   - Applies a ±0.5px horizontal subpixel jitter offset in `vkCmdSetViewport` on alternate frames.
   - Works universally across all Vulkan drivers without requiring extension support.
2. **Strategy B (Programmable Sample Locations):**
   - Keeps the viewport stationary at integer pixel coordinates.
   - Programmatically alters 2× MSAA sample positions using `vkCmdSetSampleLocationsEXT` (`VK_EXT_sample_locations`).
   - Even frames: $(0.75, 0.75)$ and $(0.25, 0.25)$; Odd frames: $(0.25, 0.75)$ and $(0.75, 0.25)$.

### 2.3 AOB Camera Pattern Scanner
For automated camera projection matrix interception:
- `pattern_scanner.cpp` performs IDA-style byte signature scanning with wildcards across the game executable memory space.
- Resolves RIP-relative instruction offsets (`ResolveRipRelative`) to acquire camera projection matrices dynamically.
- Protected by Structured Exception Handling (`__try` / `__except`) and `VirtualQuery` memory page bounds checking.

### 2.4 DirectX 12 Interception Architecture
On DX12, the mod hooks the VMT (Virtual Method Table) of:
- `IDXGISwapChain3::Present` (VMT Index 8, obtained via lightweight dummy device)
- `ID3D12CommandQueue::ExecuteCommandLists`
- `ID3D12Device::CreateCommittedResource`

---

## 3. GPU Memory Management & Buffers

### 3.1 Buffer Allocation Specifications (at 3840×2160 Target)

| Resource Identifier | Dimensions | Format | Usage / Flags | Memory Size |
|---|---|---|---|---|
| `QuarterColorMSAA` | 1920×1080 | `VK_FORMAT_R16G16B16A16_SFLOAT` (2× MSAA) | Color Attachment, Sampled | ~31.64 MB |
| `QuarterDepthMSAA` | 1920×1080 | `VK_FORMAT_D32_SFLOAT` (2× MSAA) | Depth Attachment, Sampled | ~15.82 MB |
| `HistoryColor_A` | 3840×2160 | `VK_FORMAT_R16G16B16A16_SFLOAT` | Storage Image, Sampled | ~63.28 MB |
| `HistoryColor_B` | 3840×2160 | `VK_FORMAT_R16G16B16A16_SFLOAT` | Storage Image, Sampled | ~63.28 MB |
| `HistoryDepth` | 3840×2160 | `VK_FORMAT_R32_SFLOAT` | Storage Image, Sampled | ~31.64 MB |
| `ResolvedColorOut` | 3840×2160 | `VK_FORMAT_R16G16B16A16_SFLOAT` | Storage Image, Transfer Src | ~63.28 MB |
| **Total Added VRAM** | | | | **~268.95 MB** |

*All memory is allocated via `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` using a dedicated sub-allocator.*

---

## 4. Mathematical Formulation of the Checkerboard Reconstruction

### 4.1 Subpixel Sample Offset
For a target output canvas of size $W \times H$:
$$\delta_x = \frac{1.0}{W}, \quad \delta_y = \frac{1.0}{H}$$
In the 2× MSAA quarter-resolution pass, the two sample positions in normalized pixel space are:
$$S_0 = \left(-0.25 \delta_x, -0.25 \delta_y\right), \quad S_1 = \left(+0.25 \delta_x, +0.25 \delta_y\right)$$
Between consecutive frames $t$ and $t-1$, the viewport projection matrix is perturbed by:
$$\mathbf{Jitter}(t) = \begin{cases} (+0.5 \delta_x, +0.5 \delta_y) & \text{if } t \pmod 2 = 1 \\ (-0.5 \delta_x, -0.5 \delta_y) & \text{if } t \pmod 2 = 0 \end{cases}$$

### 4.2 Parity Test
For any full-resolution pixel coordinate $(x, y) \in [0, W-1] \times [0, H-1]$:
$$\text{Phase}(x, y) = (x + y) \pmod 2$$
$$\text{ActiveFramePhase}(t) = t \pmod 2$$
- If $\text{Phase}(x, y) == \text{ActiveFramePhase}(t)$:
  The pixel coordinate directly corresponds to a sampled pixel in the current frame's 2× MSAA buffer.
- If $\text{Phase}(x, y) \neq \text{ActiveFramePhase}(t)$:
  The pixel was not shaded in the current frame and must be reconstructed temporally or spatially.

### 4.3 Motion Vector Reprojection & Velocity Sampling
Using screen-space coordinates $(u, v) \in [0, 1]^2$:
$$\mathbf{v} = \text{Sample}(\text{VelocityTexture}, u, v).\text{xy}$$
$$(u_{\text{hist}}, v_{\text{hist}}) = (u, v) - \mathbf{v}$$

### 4.4 Disocclusion & Linearized Depth Delta Validation
Let $Z_{\text{lin,curr}} = \text{LinearizeDepth}(Z_{\text{curr}})$ and $Z_{\text{lin,prev}} = \text{LinearizeDepth}(Z_{\text{prev}})$ (supporting standard and Reversed-Z with infinite far plane via `depth_convention.h`):
$$\Delta Z_{\text{lin}} = \frac{|Z_{\text{lin,curr}} - Z_{\text{lin,prev}}|}{\max(Z_{\text{lin,curr}}, 10^{-5}) \cdot \max(\text{DepthTolerance}, 10^{-4})}$$
If $\Delta Z_{\text{lin}} > 1.0$:
- **Disocclusion Triggered:** History sample is invalid. Color is derived via spatial cross-bilateral filter from the cardinal/diagonal active samples:
  $$C_{\text{spatial}} = \frac{\sum_{k=1}^4 w_k \cdot C_k}{\sum_{k=1}^4 w_k}, \quad w_k = \exp\left(-\frac{|Z_{\text{lin,curr}} - Z_{\text{lin},k}|}{\max(Z_{\text{lin,curr}}, 10^{-5}) \cdot \max(\text{DepthTolerance}, 10^{-4})}\right)$$
  - For active samples triggering fallback, the native current color and depth are preserved directly rather than reading uninitialized neighbor buffers.

### 4.5 Neighborhood Variance Color Clipping
To eliminate ghosting on dynamic objects and moving shadows across static surfaces without over-clamping sharp details, the temporal history sample $C_{\text{hist}}$ is converted to YCoCg (or RGB when configured):
$$Y = \frac{R}{4} + \frac{G}{2} + \frac{B}{4}, \quad Co = \frac{R}{2} - \frac{B}{2}, \quad Cg = -\frac{R}{4} + \frac{G}{2} - \frac{B}{4}$$
The sample neighborhood mean $\mathbf{\mu}$ and standard deviation $\mathbf{\sigma}$ are computed across the active samples:
$$\mathbf{\mu} = \frac{1}{N} \sum_{i=1}^N \mathbf{YCoCg}_i, \quad \mathbf{\sigma} = \sqrt{\frac{1}{N} \sum_{i=1}^N (\mathbf{YCoCg}_i - \mathbf{\mu})^2}$$
$$\mathbf{YCoCg}_{\min} = \mathbf{\mu} - \gamma \mathbf{\sigma}, \quad \mathbf{YCoCg}_{\max} = \mathbf{\mu} + \gamma \mathbf{\sigma} \quad (\gamma = 1.25\text{ via } \texttt{kVarianceClipGamma})$$
$$\mathbf{YCoCg}_{\text{clamped}} = \text{clamp}(\mathbf{YCoCg}_{\text{hist}}, \mathbf{YCoCg}_{\min}, \mathbf{YCoCg}_{\max})$$
Converted back to RGB before history blending and output image storage.
- Variance clipping is applied unconditionally (without `isMoving` guards) to maintain checkerboard stability on static frames.

### 4.6 Debug Visualization Modes (Modes 0–5)
Both the single-pixel and quad-based compute shaders implement 6 real-time visualization modes controlled via `u_DebugView`:
- **Mode 0 (Normal):** Fully reconstructed high-fidelity CBR image.
- **Mode 1 (Checkerboard Mask):** White ($1.0, 1.0, 1.0$) for natively shaded samples this frame; dark grey ($0.05, 0.05, 0.05$) for temporally reconstructed pixels.
- **Mode 2 (Disocclusion Heatmap):** Green ($0.1, 0.9, 0.1$) for valid temporal history reprojection; Red ($1.0, 0.1, 0.1$) for disoccluded pixels resolving via spatial cross-bilateral filter.
- **Mode 3 (Motion Vectors):** Normalized screen-space velocity vector field scaled by 50×.
- **Mode 4 (Raw Quarter-Resolution):** Unfiltered quarter-resolution color showing raw native samples.
- **Mode 5 (Spatial Baseline Resolve):** Pure spatial resolve (unweighted average of 4 cardinal native samples) enabling direct A/B image quality comparisons against temporal CBR.

---

## 5. GPU Architecture Optimizations & Compute Kernels

### 5.1 Single-Pixel Compute Pipeline (`cbr_reconstruct.comp` / `cbr_reconstruct.hlsl`)
1. **Warp Size & Thread Group Dimensions:**
   - Workgroup size configured to $16 \times 16 = 256$ threads ($8$ warps per workgroup), providing 100% occupancy on Pascal Streaming Multiprocessors (SMs) and 4 full wavefronts on AMD GCN 5.0 (Vega Wave64).
2. **Register Pressure Management:**
   - Shaders limited to $\le 32$ VGPRs per thread to prevent local memory register spills.
3. **Half-Precision (FP16) Considerations:**
   - Pascal GP104 lacks hardware FP16 (1:64 rate); internal calculations use FP32 floats, packing final outputs into `R16G16B16A16_SFLOAT`.

### 5.2 Quad-Based Compute Pipeline (`cbr_reconstruct_quad.comp`)
1. **1 Quad (2×2 Pixels) per Thread:**
   - Each thread resolves 4 adjacent target pixels simultaneously, using an $8 \times 8 \times 1$ workgroup (64 threads = 256 pixels).
   - Exploits spatial proximity to drastically increase L1/L2 texture cache hit rates when sampling quarter-resolution MSAA textures.
2. **Shared Subgroup Alignment:**
   - Perfectly aligns with Wave64 (AMD Vega) and dual Wave32 (NVIDIA Pascal), reducing workgroup dispatch overhead.

---

## 6. Build, Integration & ABI Hardening Requirements

- **Compiler:** MSVC v143 (Visual Studio 2022) with `/std:c++20` and `/O2 /Oi /Ot`, or MinGW-w64 with `-std=c++20`.
- **Target Platform:** `x64` Windows.
- **ABI Safety & Assertions:**
  - Headerless ABI extraction struct `MinimalVkSwapchainCreateInfoKHR` is asserted at compile time:
    ```cpp
    static_assert(offsetof(MinimalVkSwapchainCreateInfoKHR, imageExtent) == (sizeof(void*) == 8 ? 44 : 28), "ABI drift: imageExtent offset");
    ```
  - `sType` validation verifies `sType == 1000001000` (`VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR`) before pointer dereferencing.
  - Push-constant struct `ReconstructionPushConstants` is `static_assert`ed to 80 bytes (16-byte aligned) matching shader layouts.
- **Thread Safety & Deadlock Prevention:**
  - `RenderTargetManager`: `m_initialized` write is guarded within `std::unique_lock<std::shared_mutex>` to prevent stale dimension reads.
  - `JitterManager`: Acquires `ConfigManager::GetConfig()` prior to taking `m_mutex`, enforcing a strict lock hierarchy to prevent deadlocks.
  - `Logger`: Disk `flush()` executed exclusively on `Warning` and `Error` to eliminate render-thread I/O stalls during frame execution.

## 7. Industry References & Prior Art

1. **Intel Corporation (2018):** Trapper Mcferron & Adam Lake. *Checkerboard Rendering for Real-Time Upscaling on Intel® Integrated Graphics*. Whitepaper & DirectX 12 Mini-Engine sample code. Formally derives the 2× MSAA 4-quadrant sample coverage theorem, Shade Resolve Target (SRT) for deferred pipelines, Check Shading Occlusion (CSO) vs. Assume Shading Occluded (ASO), and MipLODBias = -0.5f. Detailed in [`docs/INTEL_CBR_REFERENCE.md`](INTEL_CBR_REFERENCE.md).
2. **Ubisoft (2016):** Jalal El Mansouri. *Rendering Rainbow Six Siege*. GDC 2016. Quarter-resolution 2× MSAA with temporal anti-aliasing resolve.
3. **Guerrilla Games & Kojima Productions (2017):** Giliam de Carpentier & Kohei Ishiyama. *Decima: Advances in Lighting and AA*. SIGGRAPH 2017. 45-degree rotation grid alignment for post-processing anti-aliasing.
4. **EA DICE (2017):** Graham Wihlidal. *4K Checkerboard in Battlefield 1 and Mass Effect: Andromeda (Frostbite)*. GDC 2017. G-Buffer checkerboarding with EQAA integration.
5. **Sony Interactive Entertainment (2016):** Mark Cerny. *Inside PlayStation 4 Pro*. Hardware-level checkerboard ID buffer and custom resolve units.
