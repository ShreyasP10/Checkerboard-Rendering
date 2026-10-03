# System Requirements Document (SRD)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** SRD-RDR2CBR-001  
**Version:** 1.0  
**Parent Document:** [PRD-RDR2CBR-001](PRD.md)  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. System Overview

The **RDR2 Checkerboard Rendering Mod** is a low-level graphics interception subsystem packaged as a Windows dynamic link library (`rdr2-cbr.asi`). It operates as an intermediary translation and dispatch layer between the Rockstar Advanced Game Engine (RAGE) and the target Graphics API runtime (Vulkan 1.2+ / DirectX 12).

The system intercepts render target allocations, alters viewport and projection transforms to introduce subpixel checkerboard jitter, injects texture sampler MIP LOD biases, and executes an asynchronous compute pass immediately prior to UI composition and post-processing.

```
+--------------------------------------------------------------+
|                    RDR2.exe (Process Space)                  |
|                                                              |
|   +---------------------+        +-----------------------+   |
|   |  RAGE Render Graph  |        |  Game Camera / Engine |   |
|   +----------+----------+        +-----------+-----------+   |
|              |                               |               |
|              v                               v               |
|   +------------------------------------------------------+   |
|   |             rdr2-cbr.asi (CBR Interceptor)           |   |
|   |                                                      |   |
|   |  [Target Interceptor]  [Jitter Injector]  [LOD Bias] |   |
|   |          |                     |               |     |   |
|   |          +----------+----------+---------------+     |   |
|   |                     |                                |   |
|   |                     v                                |   |
|   |          [CBR Reconstruction Compute Pass]           |   |
|   |          (Sample 2x MSAA, History, Motion Vectors)   |   |
|   +---------------------+--------------------------------+   |
|                         |                                    |
|                         v                                    |
|   +------------------------------------------------------+   |
|   |          Graphics Driver Runtime (Vulkan / DX12)     |   |
|   +---------------------+--------------------------------+   |
|                         |                                    |
+-------------------------+------------------------------------+
                          |
                          v
        +-----------------------------------+
        |  NVIDIA Pascal GPU (GTX 1070 Ti)  |
        +-----------------------------------+
```

---

## 2. External System Interfaces

### 2.1 Graphics API Interface
- **Vulkan ICD:** Hooks into core Vulkan dispatch tables via `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`. Intercepts:
  - `vkCreateImage`, `vkCreateImageView`
  - `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`
  - `vkCmdDrawIndexed`, `vkCmdDraw`
  - `vkQueueSubmit`, `vkQueuePresentKHR`
- **DirectX 12 (D3D12):** Hooks virtual method tables (VMT) of `IDXGISwapChain3`, `ID3D12Device`, and `ID3D12GraphicsCommandList`.

### 2.2 Game Engine Hook Interface
- **ASI Loader Interface:** Standard entry point export (`DllMain`) matching ASI specifications for `dinput8.dll` / `version.dll` loaders.
- **Pattern Scanner / Signature Engine:** Scans RAGE memory space at startup to identify view/projection matrix addresses and camera uniform buffers.

### 2.3 User & Config Interface
- **Configuration File:** UTF-8 text file `cbr.ini` loaded at initial injection, with file-watcher hot reload.
- **In-Game Overlay:** ImGui rendering pipeline embedded into the active swapchain render pass.

---

## 3. High-Level Functional Requirements

| ID | Requirement Description | Priority |
|---|---|---|
| **SRD-F-01** | **Target Interception:** The system shall detect and intercept the creation of full-resolution (3840×2160) scene color and depth targets, instantiating quarter-resolution (1920×1080) targets with 2× MSAA or half-width (1920×2160) targets. | High |
| **SRD-F-02** | **Projection Jitter Injection:** The system shall inject alternating subpixel offsets into the camera projection matrix each frame (odd: $+0.5\,\text{px}$, even: $-0.5\,\text{px}$) to align 2× MSAA sample positions with the global checkerboard grid. | High |
| **SRD-F-03** | **Texture MIP LOD Bias:** The system shall apply a $-0.5$ MIP LOD bias across active scene samplers during the reduced-resolution pass to guarantee full-resolution texture sampling. | High |
| **SRD-F-04** | **Reconstruction Compute Pass:** The system shall dispatch a compute shader immediately following scene rendering to reconstruct a full 4K frame using current MSAA samples, previous frame history, and motion vectors. | High |
| **SRD-F-05** | **Double-Buffered History Management:** The system shall maintain ping-pong full-resolution history buffers (Color, Depth, Motion Vectors) without causing GPU pipeline stalls. | High |
| **SRD-F-06** | **Disocclusion Rejection:** The system shall detect disoccluded pixels via depth delta testing and extrapolate color from adjacent current-frame samples rather than reprojecting invalid history. | High |
| **SRD-F-07** | **Runtime Toggling & Fallback:** The system shall allow instant runtime toggling (via hotkey or `cbr.ini`) returning the pipeline directly to standard native or scaled rendering. | Medium |

---

## 4. Non-Functional System Requirements

### 4.1 Performance (SRD-NF-01)
- Reconstruction compute dispatch time must not exceed **2.0 ms** on a GTX 1070 Ti at 4K resolution.
- Target frame time reduction across the complete frame pipeline: **≥ 15%** in GPU-bound scenarios.

### 4.2 Memory Budget (SRD-NF-02)
- Added VRAM allocation must remain strictly **under 300 MB**:
  - History Buffer 0 (3840×2160, RGBA16F): $\approx 63.3\,\text{MB}$
  - History Buffer 1 (3840×2160, RGBA16F): $\approx 63.3\,\text{MB}$
  - Depth History (3840×2160, R32F): $\approx 31.6\,\text{MB}$
  - Motion History (3840×2160, RG16F): $\approx 31.6\,\text{MB}$
  - Quarter-res 2× MSAA Color/Depth Targets: $\approx 35.0\,\text{MB}$
  - **Total Estimated VRAM Footprint:** $\approx 224.8\,\text{MB}$ ($\le 300\,\text{MB}$ ceiling).

### 4.3 Reliability & Compatibility (SRD-NF-03)
- No crashes during resolution switching, window resizing, alt-tabbing, or display mode changes (Fullscreen, Borderless, Windowed).
- Graceful fallback: If hook initialization fails, the mod must unhook safely and allow RDR2 to run normally.

---

## 5. Hardware & Platform Baseline

### 5.1 Primary Discrete Baseline (4K Reconstruction)
- **Reference GPU:** NVIDIA GeForce GTX 1070 Ti (8 GB GDDR5, 256-bit bus, 256.3 GB/s bandwidth, Pascal GP104).
- **Driver Baseline:** NVIDIA Game Ready Driver 512.15 or newer.
- **Target Resolution:** 4K (3840×2160) reconstructed from 1080p quarter-resolution 2× MSAA.
- **Memory Overhead:** ~268.95 MB dedicated VRAM.

### 5.2 Primary Integrated / APU Baseline (1080p Reconstruction)
- **Reference APU:** AMD Radeon Vega 7 (Ryzen 5 4600G / 5600G / 4700U / 5700U, GCN 5.0 Architecture, 7 CUs, 448 Stream Processors).
- **Memory Architecture:** Unified Memory Architecture (UMA) on Dual-Channel DDR4-3200 (~45–51.2 GB/s bandwidth).
- **Target Resolution:** 1080p (1920×1080) reconstructed from 540p ($960 \times 540$) quarter-resolution 2× MSAA.
- **Memory Overhead:** ~78.79 MB shared VRAM footprint.
- **Bandwidth Optimization:** Enforces ASO (Assume Shading Occluded) mode (`EnableMotionDilation = false`) to bypass 3×3 dilation depth fetches, conserving DDR4 bus bandwidth. Detailed in [`docs/AMD_VEGA_OPTIMIZATION.md`](AMD_VEGA_OPTIMIZATION.md).

### 5.3 System & Software
- **Operating System:** Windows 10 (64-bit, Build 19041+) / Windows 11.
- **Target Game Build:** *Red Dead Redemption 2* version 1436.28 and later.
