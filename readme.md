# RDR2 Checkerboard Rendering Mod (CBR)

[![Status: Planning](https://img.shields.io/badge/status-planning-yellow.svg)]()
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-blue.svg)]()
[![API: Vulkan/DX12](https://img.shields.io/badge/API-Vulkan%20%7C%20DX12-green.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-lightgrey.svg)]()

A community-driven attempt to implement **Checkerboard Rendering (CBR)** in *Red Dead Redemption 2* on PC, bringing a PS4 Pro‑style temporal upscaling technique to modern GPUs—specifically targeting the **NVIDIA GTX 1070 Ti** and similar Pascal‑era hardware.

> **⚠️ Project Status: Research & Design Phase**
> This mod is **not yet functional**. We are currently reverse‑engineering the RAGE engine, documenting requirements, and prototyping the rendering pipeline hooks. No public builds are available.

---

## 📖 Table of Contents
- [About](#about)
- [Goals](#goals)
- [Current Status](#current-status)
- [Planned Features](#planned-features)
- [Technical Approach](#technical-approach)
- [Documentation](#documentation)
- [Requirements (Development)](#requirements-development)
- [Building](#building)
- [Contributing](#contributing)
- [Collaborators](#collaborators)
- [License](#license)
- [Acknowledgments](#acknowledgments)
- [Disclaimer](#disclaimer)

---

## About

*Red Dead Redemption 2* on the PlayStation 4 Pro uses **checkerboard rendering** to output a 4K image while rendering only half the pixels per frame. On PC, no equivalent exists—players must choose between native 4K (too demanding for mid‑range GPUs) or modern upscalers like FSR/DLSS.

This project aims to recreate the PS4 Pro’s CBR technique as a mod for RDR2 on PC. The goal is not to replace FSR, but to offer a historically accurate, console‑equivalent rendering path for enthusiasts and researchers.

**Target GPU:** GTX 1070 Ti (Pascal), though the technique should work on any GPU with compute shader support.

---

## Goals

- Implement true checkerboard rendering for RDR2’s main scene pass.
- Reconstruct a full 3840×2160 image from quarter‑resolution MSAA buffers.
- Maintain compatibility with the game’s existing TAA and post‑processing.
- Provide a runtime toggle and debug views via an ImGui overlay.
- Achieve a **≥15% frame‑time reduction** vs native 4K on a GTX 1070 Ti.
- Keep VRAM overhead under **300 MB**.

---

## Current Status

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Research & reverse‑engineering | 🔄 In Progress |
| 1 | Hook framework (Vulkan/DX12) | ⏳ Planned |
| 2 | Render target interception | ⏳ Planned |
| 3 | Projection jitter & quarter‑res render | ⏳ Planned |
| 4 | Reconstruction shader | ⏳ Planned |
| 5 | Post‑processing integration | ⏳ Planned |
| 6 | Optimization | ⏳ Planned |
| 7 | Polish & release | ⏳ Planned |

**Biggest blocker:** Access to the game’s internal motion vectors. Without them, CBR quality will be severely limited.

---

## Planned Features

- [ ] Checkerboard rendering with 2× MSAA at quarter resolution.
- [ ] One‑pixel projection jitter alternating each frame.
- [ ] MIP LOD bias of -0.5 during the reduced‑resolution pass.
- [ ] Custom compute shader for temporal reconstruction.
- [ ] History buffer with disocclusion detection.
- [ ] Runtime toggle (`cbr.ini` or ImGui).
- [ ] Debug visualizations (checkerboard pattern, reprojection mask).
- [ ] Logging to `cbr.log`.

---

## Technical Approach

The mod will be an **ASI plugin** or **DLL** that hooks the game’s graphics API calls (Vulkan preferred for Pascal control). It will:

1. Intercept creation of the main scene color/depth targets.
2. Redirect them to quarter‑resolution (1920×1080 for 4K output) with 2× MSAA.
3. Inject a checkerboard jitter into the projection matrix each frame.
4. Run a custom reconstruction shader that merges current and previous frames using motion vectors.
5. Blend the result back into the game’s post‑processing chain.

For a full technical breakdown, see the [TRD](docs/TRD.md).

---

## Documentation

Comprehensive planning documents are available in the [`docs/`](docs/) folder:

- [Product Requirements Document (PRD)](docs/PRD.md)
- [System Requirements Document (SRD)](docs/SRD.md)
- [Software Requirements Specification (SRS)](docs/SRS.md)
- [Technical Requirements Document (TRD)](docs/TRD.md)
- [Development Plan](docs/DEV_PLAN.md)
- [Risk Register](docs/RISK_REGISTER.md)

---

## Requirements (Development)

To build and test this mod, you will need:

- **Visual Studio 2022** (or Build Tools) with C++20.
- **Vulkan SDK** (latest) or **Windows 10/11 SDK** for DX12.
- **MinHook** for API hooking.
- **ImGui** (docking branch) for debug UI.
- **RDR2-ASI Template** (or similar) for plugin loading.
- A legitimate copy of **Red Dead Redemption 2** (version 1436.28+).
- **NVIDIA GTX 1070 Ti** (or similar) for primary testing.

---

## Building

> **Note:** Build instructions are placeholders until the project reaches Phase 1.

```bash
git clone https://github.com/yourusername/rdr2-cbr-mod.git
cd rdr2-cbr-mod
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
