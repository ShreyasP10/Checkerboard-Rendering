# Software Requirements Specification (SRS)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** SRS-RDR2CBR-001  
**Version:** 1.0  
**Standard:** IEEE Std 830-1998  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. Introduction

### 1.1 Purpose
This Software Requirements Specification (SRS) describes the detailed functional, mathematical, and non-functional requirements for the *Red Dead Redemption 2 Checkerboard Rendering Mod* (`rdr2-cbr.asi`).

### 1.2 Scope
The software package provides a real-time graphics interception and compute reconstruction pipeline for RDR2, delivering a 4K checkerboard-reconstructed image on Windows systems running Vulkan or DirectX 12.

### 1.3 Definitions and Acronyms
- **CBR:** Checkerboard Rendering.
- **MSAA:** Multi-Sample Anti-Aliasing.
- **MV:** Motion Vectors (screen-space velocity buffer).
- **RAGE:** Rockstar Advanced Game Engine.
- **ASI:** A renamed dynamic link library (`.asi`) loaded automatically by game loaders into the process address space.
- **Disocclusion:** The reveal of geometry in the current frame that was occluded in the previous frame, rendering temporal history invalid.

---

## 2. System Architecture & Detailed Functional Requirements

### 2.1 FR-01: Injection & Hook Lifecycle
- **FR-01.1:** The module must initialize from `DllMain` upon `DLL_PROCESS_ATTACH` in a dedicated background worker thread to prevent blocking game initialization.
- **FR-01.2:** The module must verify the host process name (`RDR2.exe`). If running in an unsupported process, it must cleanly detach without throwing exceptions.
- **FR-01.3:** Upon `DLL_PROCESS_DETACH`, all API hooks installed via MinHook must be disabled and restored to their original function pointers.

### 2.2 FR-02: Render Target Interception & Resolution Scaling
- **FR-02.1:** The system shall hook Vulkan image creation (`vkCreateImage`), command buffer viewports (`vkCmdSetViewport`), and scissors (`vkCmdSetScissor`).
- **FR-02.2:** When a candidate 3D scene geometry viewport matching output dimensions ($W \times H$, e.g., $3840 \times 2160$) is detected, the dimensions shall be dynamically downscaled to quarter resolution:
  $$W_{\text{render}} = \frac{W}{2}, \quad H_{\text{render}} = \frac{H}{2}$$
- **FR-02.3:** The intercepted render target shall be configured with `VK_SAMPLE_COUNT_2_BIT` (2× MSAA) in Vulkan, or `SampleDesc.Count = 2` in DirectX 12.
- **FR-02.4:** UI, HUD, and minimap rendering passes must be detected and preserved at native full resolution (3840×2160) without downscaling.

### 2.3 FR-03: Subpixel Jitter & Sampling Strategy
- **FR-03.1 (Strategy A - Viewport Shift Jitter):** The projection matrix or viewport shall be offset on alternating frames according to a 2-phase checkerboard sequence:
  $$\text{Offset}_x = \begin{cases} +0.5 / W_{\text{render}} & \text{if FrameIndex is Odd} \\ -0.5 / W_{\text{render}} & \text{if FrameIndex is Even} \end{cases}$$
- **FR-03.2 (Strategy B - Programmable Sample Locations):** For hardware supporting `VK_EXT_sample_locations`, the viewport is kept stationary at integer pixels while sample positions alternate per frame:
  - Even Frames: Sample 0 at $(0.75, 0.75)$, Sample 1 at $(0.25, 0.25)$
  - Odd Frames: Sample 0 at $(0.25, 0.75)$, Sample 1 at $(0.75, 0.25)$
- **FR-03.3 (Mutual Exclusivity):** Strategy A and Strategy B must remain strictly mutually exclusive to prevent destructive sample cancellation.
- **FR-03.4 (AOB Camera Pattern Scanner):** The system shall scan game memory using wildcard byte signatures to automatically locate camera projection matrix pointers with RIP-relative address resolution.

### 2.4 FR-04: Reconstruction Compute Pass
- **FR-04.1:** The reconstruction pass shall be executed as a compute shader dispatched immediately following the main scene render pass on `vkCmdEndRenderPass`.
- **FR-04.2:** Compute thread group dimensions shall be $16 \times 16 \times 1$ threads.
- **FR-04.3:** Dispatch dimension for $3840 \times 2160$:
  $$\text{Groups}_X = \lceil 3840 / 16 \rceil = 240, \quad \text{Groups}_Y = \lceil 2160 / 16 \rceil = 135$$
- **FR-04.4:** For each full-resolution pixel $(x, y)$, the shader evaluates its checkerboard phase parity:
  $$\text{PixelParity} = (x + y) \pmod 2$$
  - If $\text{PixelParity} == (\text{FrameIndex} \pmod 2)$, the pixel is **Active Current Sample** and resolved directly from the 2× MSAA buffer.
  - If $\text{PixelParity} \neq (\text{FrameIndex} \pmod 2)$, the pixel is **Missing Sample** and must be reconstructed from temporal history.

### 2.5 FR-05: Temporal History Reprojection & Depth Validation
- **FR-05.1:** For missing samples, the previous frame screen coordinate is calculated using screen-space velocity:
  $$\mathbf{UV}_{\text{prev}} = \mathbf{UV}_{\text{curr}} - \mathbf{V}(x, y)$$
- **FR-05.2:** The shader performs a depth validation test:
  $$\Delta Z = |Z_{\text{curr}}(x, y) - Z_{\text{prev}}(\mathbf{UV}_{\text{prev}})|$$
  - If $\Delta Z \le \epsilon_{\text{depth}}$ (default $\epsilon = 0.01$), reprojection is deemed **Valid**, and the color is sampled bilinearly from History Buffer $N-1$.
  - If $\Delta Z > \epsilon_{\text{depth}}$, the pixel is deemed **Disoccluded**, and the color is reconstructed via spatial cross-bilateral filter from the current frame's 4 adjacent active checkerboard samples.

### 2.6 FR-06: 3×3 Neighborhood Color Clamping
- **FR-06.1:** To suppress temporal ghosting on dynamic objects, reprojected history color $C_{\text{prev}}$ must be clamped within the color bounding box of current frame neighbors:
  $$C_{\min} = \min_{i \in \text{Kernel}} C_{\text{curr}}(i), \quad C_{\max} = \max_{i \in \text{Kernel}} C_{\text{curr}}(i)$$
  $$C_{\text{final}} = \text{clamp}(C_{\text{prev}}, C_{\min}, C_{\max})$$
- **FR-06.2:** Clamping shall be performed in the YCoCg color space to prevent chroma fringing and color bleeding.

### 2.7 FR-07: Motion Vector Acquisition & Depth Unprojection Fallback
- **FR-07.1:** The primary motion vector source shall be intercepted from the RAGE engine's internal velocity buffer.
- **FR-07.2:** If the engine velocity buffer is unavailable, the fallback module must compute camera-only velocity vectors using:
  $$\mathbf{P}_{\text{world}} = \text{InverseProjectionMatrix}_{\text{curr}} \times \mathbf{P}_{\text{ndc}}$$
  $$\mathbf{P}_{\text{prev\_ndc}} = \text{ViewProjectionMatrix}_{\text{prev}} \times \mathbf{P}_{\text{world}}$$
  $$\mathbf{V}_{\text{fallback}} = \mathbf{P}_{\text{ndc}} - \mathbf{P}_{\text{prev\_ndc}}$$

### 2.8 FR-08: MIP LOD Bias Injection
- **FR-08.1:** Intercept sampler creation and descriptor updates (`vkCreateSampler`, `CreateSampler`) to inject a constant `mipLodBias = -1.0f`.
- **FR-08.2:** The bias ensures that textures retain full 4K Nyquist frequency details during quarter-resolution shading passes.

### 2.9 FR-09: ImGui Overlay & Diagnostic Modes
- **FR-09.1:** Toggleable overlay activated via keypress (default: `F11` or `Insert`).
- **FR-09.2:** Supported debug visualizers:
  - Mode 0: Normal CBR Output.
  - Mode 1: Checkerboard Sample Pattern (visualizing alternating subpixel grid).
  - Mode 2: Disocclusion Heatmap (Green = Valid Reprojection, Red = Disoccluded Fallback).
  - Mode 3: Motion Vector Vector Field.

### 2.10 FR-10: Configuration File
- **FR-10.1:** Settings read from `cbr.ini` placed alongside `RDR2.exe`:
  ```ini
  [General]
  Enabled = true
  TargetWidth = 3840
  TargetHeight = 2160
  PreferredApi = Vulkan
  SamplingStrategy = StrategyA
  MipLodBias = -1.0

  [Reconstruction]
  DepthTolerance = 0.010
  EnableColorClamping = true
  ColorSpace = YCoCg
  HistoryWeight = 0.90
  EnableSpatialFallback = true
  EnableMotionDilation = true

  [Debug]
  ShowOverlay = false
  DebugView = 0
  LogToFile = true
  ```

---

## 3. Non-Functional Software Requirements

### 3.1 Performance Requirements
- GPU compute time for the resolve pass must be $\le 1.8\,\text{ms}$ on GTX 1070 Ti at 4K.
- CPU hook overhead per draw call $\le 0.05\,\mu\text{s}$.

### 3.2 Threading & Safety
- All hook callbacks must be reentrant and thread-safe across multiple command recording threads.
- History ping-pong buffer transitions must include appropriate execution and memory barriers (`VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT`).

---

## 4. Requirements Traceability Matrix

| Requirement | Description | Status | Implementation Component |
|---|---|---|---|
| **FR-01** | Injection & Lifecycle | **VERIFIED** | `main.cpp`, `hooks.cpp`, `crash_handler.cpp` |
| **FR-02** | Render Target & Viewport Interception | **COMPLETE** | `hooks_vulkan.cpp`, `render_target_manager.cpp` |
| **FR-03** | Subpixel Jitter & Sampling Strategy | **COMPLETE** | `jitter_manager.cpp`, `hooks_vulkan.cpp`, `pattern_scanner.cpp` |
| **FR-04** | Reconstruction Compute Pass | **COMPLETE** | `reconstruction_pass.cpp`, `cbr_reconstruct.comp`, `cbr_reconstruct_quad.comp` |
| **FR-05** | Temporal Reprojection & Depth Test | **COMPLETE** | `cbr_reconstruct.comp`, `cbr_reconstruct.hlsl` |
| **FR-06** | YCoCg Neighborhood Color Clamping | **COMPLETE** | `cbr_reconstruct.comp`, `cbr_reconstruct.hlsl` |
| **FR-07** | Motion Vector Acquisition & Fallback | **IN PROGRESS** | G-Buffer velocity hook complete; matrix fallback queued |
| **FR-08** | MIP LOD Bias (-1.0f) | **COMPLETE** | `config.h`, `cbr.ini`, sampler intercept |
| **FR-09** | ImGui Overlay & Diagnostic Modes | **IN PROGRESS** | `ui_overlay.cpp`, shaders (Modes 0–3 implemented) |
| **FR-10** | Configuration Management | **COMPLETE** | `config.cpp`, `cbr.ini` |

