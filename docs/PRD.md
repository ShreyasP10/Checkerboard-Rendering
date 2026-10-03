# Product Requirements Document (PRD)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** PRD-RDR2CBR-001  
**Version:** 1.0  
**Status:** Approved  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. Executive Summary

This document specifies the product requirements for a community modification to *Red Dead Redemption 2* (RDR2) on PC, implementing **Checkerboard Rendering (CBR)**. Originating from console architectures such as the PlayStation 4 Pro, CBR renders only half the pixels of the target resolution (1920×2160 for 4K target output, or quarter-resolution with 2× MSAA) in an alternating checkerboard pattern across odd and even frames, then temporally reconstructs a full 3840×2160 frame.

This mod targets Pascal-era and mid-range PC hardware—specifically the **NVIDIA GeForce GTX 1070 Ti**—offering a native console-faithful rendering path that balances high pixel density against restricted compute budgets.

---

## 2. Problem Statement

1. **Native 4K Demands:** RDR2 is one of the most graphically demanding PC titles. Running at native 4K (3840×2160) causes sub-30 FPS performance on mid-tier GPUs like the GTX 1070 Ti.
2. **Upscaler Incompatibilities & Profile Differences:**
   - NVIDIA DLSS is restricted to RTX hardware (Tensor cores) and cannot run on Pascal GPUs (GTX 10-series).
   - AMD FSR 2/3 provides temporal upscaling but introduces its own post-processing aesthetic and edge characteristics.
   - Traditional spatial upscaling (FSR 1, Bilinear, Bicubic) produces significant blur and loss of high-frequency detail.
3. **Absence of Console CBR on PC:** The PS4 Pro version of RDR2 runs at an effective 1920×2160 checkerboard presentation. PC players have never had access to this rendering pipeline option.

---

## 3. Product Vision & Goals

- **Vision:** Provide a seamless, plug-and-play ASI plugin for RDR2 PC that reproduces PS4 Pro-style Checkerboard Rendering with high temporal stability, low latency, and full fidelity.
- **Primary Goals:**
  - Render the primary scene at 50% shading cost (quarter-res 2× MSAA or 1920×2160).
  - Reconstruct a full 3840×2160 output via a high-performance compute shader.
  - Achieve ≥15% GPU frame-time reduction vs native 4K on a GTX 1070 Ti.
  - Maintain an additional VRAM footprint under 300 MB.
  - Seamlessly hook into the game without requiring modified game files or online circumvention.

---

## 4. Target Audience & Hardware Persona

| Persona | Description | Primary Hardware | Use Case |
|---|---|---|---|
| **Enthusiast Gamer** | Plays RDR2 on a 4K TV or high-res monitor using older hardware. | GTX 1070 Ti, GTX 1080, RX 590 | Desires 4K presentation with smooth 45–60 FPS. |
| **APU / Budget Gamer** | Plays RDR2 on laptops or budget PCs with integrated graphics. | AMD Radeon Vega 7 (Ryzen 5 4600G/5600G/5700U), Vega 8 | Desires 1080p 48–60 FPS reconstructed from 540p. |
| **Graphics Modder / Researcher** | Graphics engineers, modders, and computer vision students. | Pascal, Turing, RDNA2, GCN 5.0 | Benchmarking temporal reconstruction vs FSR/DLSS. |
| **Console Purist** | Players wanting the authentic PS4 Pro aesthetic on PC. | Any Vulkan/DX12 GPU | Nostalgic/authentic visual recreation. |

---

## 5. Success Metrics & Key Performance Indicators (KPIs)

1. **Performance:**
   - Minimum **15% to 25% lower GPU frame times** in dense scenes (e.g., Saint Denis, Valentine, dense forests) compared to native 4K.
   - Reconstruction pass compute dispatch time **< 1.8 ms** at 4K on a GTX 1070 Ti.
2. **Visual Fidelity:**
   - Structural Similarity Index (SSIM) ≥ 0.88 against native 4K ground truth in static scenes.
   - Elimination of visible checkerboard crawling/flicker at 60 Hz display refresh.
   - Suppression of ghosting on high-contrast moving edges (horses, hair, foliage) via neighborhood color clamping.
3. **Stability & Usability:**
   - 100% crash-free operation over a 2-hour continuous open-world gameplay session.
   - Hot-reloading and runtime toggle via `cbr.ini` and an in-game ImGui overlay.

---

## 6. Scope & Feature Boundaries

### In Scope
- ASI plugin architecture compatible with standard RDR2 ASI loaders (`dinput8.dll` / `version.dll`).
- Interception of main scene G-Buffer / forward render targets in Vulkan and DirectX 12.
- Subpixel projection jitter injector (alternating grid offset per frame).
- Compute shader-based temporal reconstruction with disocclusion rejection.
- Texture MIP LOD bias injection (-0.5 bias) to preserve fine texture details.
- In-game ImGui debug overlay (visualize checkerboard grid, motion vectors, disocclusion masks).

### Out of Scope
- Multiplayer / Red Dead Online compatibility (strictly single-player/offline to avoid anti-cheat violations).
- Modification of game assets, art, textures, or gameplay logic.
- Direct ML/AI hardware acceleration (designed purely as a standard compute shader).

---

## 7. Assumptions & Dependencies

- **Engine:** Rockstar Advanced Game Engine (RAGE) running on PC version 1436.28 or newer.
- **APIs:** Vulkan (primary, optimal for Pascal) and DirectX 12 (secondary).
- **Motion Vectors:** Intercepted from the game's internal TAA buffer or reconstructed via camera matrix reprojection + depth differences.
- **Legal:** Strictly offline, educational, and open-source under the MIT license.
