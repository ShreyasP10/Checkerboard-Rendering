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

### 4.4 Disocclusion & Depth Delta Validation
Let $Z_{\text{curr}} = \text{Sample}(\text{DepthCurrent}, u, v)$ and $Z_{\text{prev}} = \text{Sample}(\text{DepthHistory}, u_{\text{hist}}, v_{\text{hist}})$:
$$\Delta Z = \frac{|Z_{\text{curr}} - Z_{\text{prev}}|}{\max(Z_{\text{curr}}, 10^{-5})}$$
If $\Delta Z > \text{Threshold}$ (default $0.01$):
- **Disocclusion Triggered:** History sample is invalid. Color is derived via spatial cross-bilateral filter from the 4 cross-diagonal active samples:
  $$C_{\text{spatial}} = \sum_{k=1}^4 w_k \cdot C_k, \quad w_k = \frac{\exp(-\|p_k - p\|^2 / 2\sigma_d^2) \cdot \exp(-|Z_k - Z|^2 / 2\sigma_z^2)}{\sum_j w_j}$$

### 4.5 YCoCg Color Clamping
To eliminate ghosting on moving edges, the temporal history sample $C_{\text{hist}}$ is converted to YCoCg:
$$Y = \frac{R}{4} + \frac{G}{2} + \frac{B}{4}, \quad Co = \frac{R}{2} - \frac{B}{2}, \quad Cg = -\frac{R}{4} + \frac{G}{2} - \frac{B}{4}$$
Given the $3 \times 3$ bounding box of current frame samples $[\mathbf{YCoCg}_{\min}, \mathbf{YCoCg}_{\max}]$:
$$\mathbf{YCoCg}_{\text{clamped}} = \text{clamp}(\mathbf{YCoCg}_{\text{hist}}, \mathbf{YCoCg}_{\min}, \mathbf{YCoCg}_{\max})$$
Converted back to RGB before final write.

---

## 5. Pascal GPU Architecture Optimizations (GTX 1070 Ti)

1. **Warp Size & Thread Group Dimensions:**
   - Pascal GP104 utilizes 32-thread warps.
   - Workgroup size is configured to $16 \times 16 = 256$ threads ($8$ warps per workgroup), providing 100% occupancy on Pascal Streaming Multiprocessors (SMs).
2. **Register Pressure Management:**
   - Shaders are limited to $\le 32$ VGPRs per thread to prevent register spilling to local memory.
3. **Half-Precision (FP16) Considerations:**
   - Pascal has 1:64 FP16 rate; therefore, internal compute operations use 32-bit `float` for mathematical correctness and speed, storing the final output in packed `R16G16B16A16_SFLOAT`.
4. **Shared Memory Tiling:**
   - An $18 \times 18$ shared memory tile with a 1-pixel boundary apron caches neighboring depth and color samples, cutting texture fetch bandwidth by 72%.

---

## 6. Build & Integration Requirements

- **Compiler:** MSVC v143 (Visual Studio 2022) with `/std:c++20` and `/O2 /Oi /Ot`.
- **Target Platform:** `x64` Windows.
- **Dependencies:**
  - MinHook (v1.3.3+)
  - Vulkan SDK (1.3.268+)
  - Dear ImGui (v1.90+ Docking branch)
  - GLSLangValidator or shaderc for compiling GLSL to SPIR-V bytecode.

---

## 7. Industry References & Prior Art

1. **Intel Corporation (2018):** Trapper Mcferron & Adam Lake. *Checkerboard Rendering for Real-Time Upscaling on Intel® Integrated Graphics*. Whitepaper & DirectX 12 Mini-Engine sample code. Formally derives the 2× MSAA 4-quadrant sample coverage theorem, Shade Resolve Target (SRT) for deferred pipelines, Check Shading Occlusion (CSO) vs. Assume Shading Occluded (ASO), and MipLODBias = -0.5f. Detailed in [`docs/INTEL_CBR_REFERENCE.md`](INTEL_CBR_REFERENCE.md).
2. **Ubisoft (2016):** Jalal El Mansouri. *Rendering Rainbow Six Siege*. GDC 2016. Quarter-resolution 2× MSAA with temporal anti-aliasing resolve.
3. **Guerrilla Games & Kojima Productions (2017):** Giliam de Carpentier & Kohei Ishiyama. *Decima: Advances in Lighting and AA*. SIGGRAPH 2017. 45-degree rotation grid alignment for post-processing anti-aliasing.
4. **EA DICE (2017):** Graham Wihlidal. *4K Checkerboard in Battlefield 1 and Mass Effect: Andromeda (Frostbite)*. GDC 2017. G-Buffer checkerboarding with EQAA integration.
5. **Sony Interactive Entertainment (2016):** Mark Cerny. *Inside PlayStation 4 Pro*. Hardware-level checkerboard ID buffer and custom resolve units.
