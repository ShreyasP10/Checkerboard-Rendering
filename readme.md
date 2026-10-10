# RDR2 Checkerboard Rendering Mod (CBR)

[![Status: Planning & Prototyping](https://img.shields.io/badge/status-prototyping-yellow.svg)]()
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-blue.svg)]()
[![API: Vulkan / DX12](https://img.shields.io/badge/API-Vulkan%20%7C%20DX12-green.svg)]()
[![Target: GTX 1070 Ti](https://img.shields.io/badge/Target-GTX%201070%20Ti%20(Pascal)-orange.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

A community-driven graphics modification implementing **Checkerboard Rendering (CBR)** in *Red Dead Redemption 2* (PC), bringing the PlayStation 4 Pro's hardware-assisted temporal reconstruction technique to modern PC GPUs—specifically targeting the **NVIDIA GeForce GTX 1070 Ti** and Pascal-architecture hardware.

> **⚠️ Project Status: Prototyping & Pipeline Architecture Phase**  
> The engineering specifications, reconstruction compute shaders, configuration system, logging and unit tests are written. **The graphics hooks are not implemented yet, so the mod currently has no effect in-game** (it loads, reads `cbr.ini`, writes `cbr.log` and then waits for hooks that do not exist). The install and in-game-settings steps below describe the intended setup once game integration lands. Public release builds will follow game integration validation.

---

## 📖 Table of Contents
- [About](#about)
- [Why Checkerboard Rendering? (CBR vs. DLSS / FSR)](#why-checkerboard-rendering-cbr-vs-dlss--fsr)
- [How It Works](#how-it-works)
- [Key Features](#key-features)
- [Architecture & Repository Structure](#architecture--repository-structure)
- [Complete Source Codebase (Single Document)](CODEBASE.md)
- [Engineering Documentation](#engineering-documentation)
- [Reconstruction Shader Math](#reconstruction-shader-math)
- [Hardware & Development Requirements](#hardware--development-requirements)
- [How to Install & Run](#-how-to-install--run)
- [Configuration](#configuration-cbrini)
- [Testing](#testing)
- [Changelog](#changelog--recent-updates-v010-alpha)
- [Contributing](#contributing)
- [Collaborators & Maintainers](#collaborators--maintainers)
- [License](#license)
- [Disclaimer](#disclaimer)

---

## About

On the PlayStation 4 Pro, *Red Dead Redemption 2* outputs a 4K presentation by rendering only half the pixels per frame (1920×2160 or quarter-resolution with 2× MSAA) in an alternating checkerboard pattern, reconstructing missing details across time. 

On PC, players on mid-range GPUs such as the **NVIDIA GeForce GTX 1070 Ti** face a difficult dilemma:
- **Native 4K (3840×2160)** is too demanding for 60 FPS gameplay on Pascal hardware.
- **DLSS** requires RTX hardware (Tensor cores) and cannot run on Pascal GPUs.
- **FSR** provides spatial/temporal upscaling but has a distinct aesthetic and does not replicate the console presentation.

This project delivers a **native ASI plugin** that intercepts RDR2's rendering passes, renders the primary geometry at half shading cost, and reconstructs a full 4K frame using an optimized compute shader with temporal reprojection, depth disocclusion detection, and YCoCg neighborhood color clamping.

---

## Why Checkerboard Rendering? (CBR vs. DLSS / FSR)

**Inspiration: The PlayStation 4 Pro**  
When *Red Dead Redemption 2* launched on the PS4 Pro, it achieved a gorgeous 4K presentation despite the console's mid-range GPU architecture. It accomplished this using **Checkerboard Rendering (CBR)**: shading only half the pixels of a 4K frame (using a 2× MSAA alternating grid pattern) and mathematically reconstructing the missing pixels using historical frame data. This mod brings that exact algorithmic elegance and console presentation to the PC version of RDR2.

**How is CBR different from DLSS or FSR?**  
Modern upscaling solutions (like DLSS, FSR, and XeSS) operate on lower-resolution flat 2D buffers (e.g., rendering natively at 1080p and scaling up to 4K). 
* **DLSS / XeSS:** Rely on AI machine learning and specialized hardware (Tensor cores) to hallucinate missing details. They are extremely effective, but **DLSS cannot run on older hardware like the GTX 10-series (Pascal)**.
* **FSR 2.0+:** A spatial-temporal algorithmic upscaler that runs on any GPU. However, because it reconstructs a full 4K image from a standard, lower-resolution flat grid, it introduces a distinctly different aesthetic, often resulting in over-sharpening, shimmering, or loss of high-frequency geometry details.

**What extra benefits does CBR provide?**  
1. **Geometric Fidelity (2× MSAA):** Unlike FSR which renders a standard flat 1080p image, CBR renders using 2× MSAA arranged in a subpixel checkerboard pattern. This means that across two frames, CBR physically samples **100% of the 4K geometry**, providing native-like edge clarity without the temporal smearing of standard upscalers.
2. **Universal Compatibility & Hardware Efficiency:** CBR is a pure algorithmic math shader (utilizing YCoCg neighborhood color clamping and temporal motion vector reprojection). It requires **no AI Tensor cores** and **no proprietary hardware**, making it the ultimate solution for aging but powerful architectures like the GTX 1070 Ti, GTX 1080, and AMD Vega APUs.
3. **The Authentic "Console" Look:** It exactly replicates the specific temporal aesthetic of the PS4 Pro, giving players the intended cinematic console experience with PC framerates.

---

## How It Works

```
Frame N:     [ X ] [   ] [ X ] [   ]  <-- Shaded at Quarter-Res 2x MSAA + Jitter
             [   ] [ X ] [   ] [ X ]
               |
               v
Reconstruct: Current Active Pixels  <─── Keep 2x MSAA Samples
             Missing Holes          <─── Sample Previous Frame (N-1) using Motion Vectors
                                         [Depth Validation & 3x3 YCoCg Clamping]
               |
               v
Output:      Full 3840×2160 4K Image
```

1. **Target Interception:** Intercepts main scene color and depth attachments, routing them to quarter-resolution (1920×1080) targets configured with 2× MSAA.
2. **Projection Jitter:** Alternates the camera projection matrix horizontally by 1 full-resolution pixel on odd frames (0 on even frames), shifting the 2× MSAA subpixel grid to achieve 100% 4-quadrant geometric coverage across two frames.
3. **MIP LOD Bias Injection:** Injects a $-0.5$ LOD bias into scene texture samplers, ensuring high-frequency textures sample at full 4K Nyquist clarity despite reduced geometry resolution.
4. **Compute Shader Reconstruction:** Runs a compute shader (`cbr_reconstruct.comp` / `cbr_reconstruct.hlsl`) that evaluates pixel parity, reprojects history using motion vectors, tests depth for disocclusion, and clamps against the 3×3 color neighborhood.

---

## Key Features

- [x] Full architectural specification & requirements documentation (PRD, SRD, SRS, TRD, DEV_PLAN, RISK_REGISTER).
- [x] Complete GLSL & HLSL reconstruction compute shaders with 2× MSAA unpack and subpixel parity testing.
- [x] Temporal reprojection math with velocity vector sampling.
- [ ] Camera depth unprojection fallback (not implemented).
- [x] Depth delta disocclusion detection with spatial cross-bilateral filter fallback.
- [x] 3×3 neighborhood color bounding box clamping (YCoCg or RGB, selectable via `ColorSpace`) to suppress ghosting.
- [x] Reconstruction pass writes a full-resolution history-depth target for the next frame's disocclusion test.
- [x] Closest-depth motion-vector dilation, variance clipping and jitter-compensated history reprojection in the shaders (validated by compilation and unit-tested plumbing only; the jitter sign convention is configurable via `JitterCompensation` and must be confirmed in-game).
- [x] Host-side unit tests (config, logger, jitter, VRAM accounting) and a CI workflow; GLSL validated with glslang, HLSL gated by the glslang front-end plus `dxc` when installed.
- [x] Pascal-oriented design (16×16 thread groups, guarded neighborhood fetches to cut bandwidth).
- [ ] Shared-memory tiling (not implemented; neighborhood data is fetched directly from the MSAA targets).
- [x] Runtime configuration via `cbr.ini`.
- [ ] In-game ImGui debug overlay (placeholder only; no ImGui integration or F11/Insert key handling yet).
- [x] Multi-mode debug visualizer (checkerboard grid mask, disocclusion heatmap, motion vector field).
- [ ] Live Vulkan & DirectX 12 function hooking & engine command buffer interception (Pending RDR2 dynamic address offset resolution).
- [ ] Automated game projection matrix offset injection (Pending RDR2 script hook / pattern scan integration).

---

## Architecture & Repository Structure

```
Checkerboard-Rendering/
├── CMakeLists.txt              # CMake build script for rdr2-cbr.asi
├── LICENSE                     # MIT License
├── README.md                   # Project overview and instructions
├── CODEBASE.md                 # Consolidated single document of all source code & shaders
├── CONTRIBUTING.md             # Contribution guidelines & coding standards
├── cbr.ini                     # Runtime configuration file
├── .gitignore                  # Git ignore rules
│
├── docs/                       # Comprehensive engineering documentation
│   ├── PRD.md                  # Product Requirements Document
│   ├── SRD.md                  # System Requirements Document
│   ├── SRS.md                  # Software Requirements Specification (IEEE 830)
│   ├── TRD.md                  # Technical Requirements Document
│   ├── DEV_PLAN.md             # Phased Development Roadmap & Milestones
│   └── RISK_REGISTER.md        # Risk Analysis & Mitigation Strategies
│
├── include/cbr/                # C++ Architecture Headers
│   ├── cbr_engine.h            # Core engine controller & frame lifecycle
│   ├── hooks.h                 # Vulkan & DX12 API hook declarations
│   ├── render_target_manager.h # Intermediate MSAA & history buffer manager
│   ├── jitter_manager.h        # Projection matrix jitter calculator
│   ├── reconstruction_pass.h   # Compute shader dispatch & pipeline manager
│   ├── config.h                # cbr.ini configuration reader & settings (canonical limits live here)
│   ├── limits.h                # Optional backwards-compat shim (forwards to config.h; not required to build)
│   ├── logger.h                # Thread-safe cbr.log file logger
│   └── ui_overlay.h            # ImGui in-game debug overlay
│
├── src/                        # C++ Implementation
│   ├── main.cpp                # DLL entry point (DllMain) & loader integration
│   ├── cbr_engine.cpp          # Pipeline orchestration
│   ├── hooks.cpp               # Hook manager core & lifecycle
│   ├── hooks_vulkan.cpp        # Vulkan API hooks (vkQueuePresentKHR, vkCmdDraw, etc.)
│   ├── hooks_dx12.cpp          # DirectX 12 hooks (Present, ExecuteCommandLists, etc.)
│   ├── render_target_manager.cpp # VRAM allocation & ping-pong history buffers
│   ├── jitter_manager.cpp      # Subpixel matrix perturbation
│   ├── reconstruction_pass.cpp # Compute pipeline dispatch
│   ├── config.cpp              # Configuration file parser
│   ├── logger.cpp              # Logger implementation
│   └── ui_overlay.cpp          # ImGui overlay rendering
│
└── shaders/                    # GPU Reconstruction Shaders
    ├── cbr_reconstruct.comp    # Complete GLSL Vulkan compute shader
    ├── cbr_reconstruct.hlsl    # Complete HLSL DirectX 12 compute shader
    └── cbr_resolve_simple.comp # Spatial-only fallback resolve shader
```

---

## Engineering Documentation

Detailed specifications are maintained in the [`docs/`](docs/) directory:

- 📄 [**Product Requirements Document (PRD)**](docs/PRD.md) – Problem statement, target personas, KPIs, and scope.
- 📄 [**System Requirements Document (SRD)**](docs/SRD.md) – Subsystem architecture, external interfaces, and VRAM budget.
- 📄 [**Software Requirements Specification (SRS)**](docs/SRS.md) – Detailed functional requirements, mathematical formulas, and IEEE 830 standards.
- 📄 [**Technical Requirements Document (TRD)**](docs/TRD.md) – Vulkan/DX12 hook mechanics, buffer formats, and Pascal GPU optimizations.
- 📄 [**Development Plan (DEV_PLAN)**](docs/DEV_PLAN.md) – 8-phase roadmap, milestones, and deliverable schedules.
- 📄 [**Risk Register (RISK_REGISTER)**](docs/RISK_REGISTER.md) – Assessment of motion vector extraction, Pascal bandwidth, and mitigations.
- 📄 [**Intel CBR Reference Analysis (INTEL_CBR_REFERENCE)**](docs/INTEL_CBR_REFERENCE.md) – Reference breakdown of Mcferron & Lake (Intel 2018): 2× MSAA 4-quadrant geometry, Shade Resolve Targets (SRT), and linear depth disocclusion.
- 📄 [**AMD Radeon Vega & Vega 7 Optimization Guide (AMD_VEGA_OPTIMIZATION)**](docs/AMD_VEGA_OPTIMIZATION.md) – Hardware profile for AMD Radeon Vega 7 APUs: GCN 5.0 Wave64 scheduling, Rapid Packed Math (FP16), DDR4 memory bandwidth throttling (ASO mode), and 1080p preset.

---

## Reconstruction Shader Math

The core compute shader resolves pixels based on parity:

$$\text{Phase}(x, y) = (x + y) \pmod 2$$

- When $\text{Phase}(x, y) = (\text{FrameIndex} \pmod 2)$, the pixel is sampled directly from the current frame's 2× MSAA buffer.
- When $\text{Phase}(x, y) \neq (\text{FrameIndex} \pmod 2)$, the pixel is reprojected from history:

$$\mathbf{UV}_{\text{prev}} = \mathbf{UV}_{\text{curr}} - \mathbf{V}(x, y)$$

If the depth variance exceeds the tolerance threshold:

$$\Delta Z = \frac{|Z_{\text{curr}} - Z_{\text{prev}}|}{\max(Z_{\text{curr}}, 10^{-5})} > \text{Threshold}$$

The shader rejects the history sample and executes a spatial cross-bilateral filter from the current frame's four orthogonal (cardinal: up, down, left, right) active samples:

$$C_{\text{spatial}} = \frac{\sum_{k=1}^4 w_k C_k}{\sum_{k=1}^4 w_k}, \quad w_k = \exp\left(-\frac{\|p_k - p\|^2}{2\sigma_d^2}\right) \cdot \exp\left(-\frac{|Z_k - Z|^2}{2\sigma_z^2}\right)$$

---

## Hardware & Development Requirements

| Component | Minimum Specification | Recommended (Target Discrete) | Recommended (Target APU / Integrated) | Role in CBR Pipeline |
|---|---|---|---|---|
| **GPU** | GTX 1060 (6 GB) / RX 580 (8 GB) | **NVIDIA GeForce GTX 1070 Ti (8 GB GDDR5)** | **AMD Radeon Vega 7 (Ryzen 5 4600G/5600G/5700U APU)** | Wave32 (Pascal) / Wave64 (Vega) compute shader execution, 2× MSAA rasterization |
| **GPU VRAM** | 1 GB (for 1080p CBR) / 6 GB (for 4K) | **8 GB GDDR5** (~300.58 MiB / 315.19 MB CBR footprint at 4K) | **512 MB – 2 GB Shared UMA DDR4** (~78.80 MB CBR footprint at 1080p) | Stores ping-pong history and intermediate MSAA targets |
| **CPU** | Quad-Core (i5-8400 / Ryzen 2600) | **6-Core / 12-Thread (i7 / Ryzen 3600+)** | **AMD Ryzen 5 4600G / 5600G (6C / 12T APU)** | Frame pacing and intercept dispatch |
| **RAM** | 8 GB Dual-Channel | **16 GB DDR4 Dual-Channel** | **16 GB Dual-Channel DDR4-3200+** | Critical on APUs for shared GPU/CPU memory bandwidth |
| **OS** | Windows 10 (64-bit, 19041+) | **Windows 10 / Windows 11 (64-bit)** | **Windows 10 / Windows 11 (64-bit)** | Native Vulkan 1.3 and DirectX 12 support |
| **Target Presentation** | 1080p | **Native 4K (3840×2160)** reconstructed from 1080p 2× MSAA | **Native 1080p (1920×1080)** reconstructed from 540p 2× MSAA | Output resolution |

---

<a id="building"></a>
## 🚀 How to Install & Run

### Option A: Download Pre-built Binaries (Recommended)
1. Go to the [Releases](../../releases) page of this repository.
2. Download the latest `rdr2-cbr-release.zip` or download `rdr2-cbr.asi` and `cbr.ini` directly from the release assets.
3. Skip to **Step 3: Install into RDR2 from Scratch**.

### Option B: Build from Source
If you prefer to compile the mod yourself, follow these steps:

#### B.1 Software Prerequisites
Ensure you have:
1. **Visual Studio 2022** (Community or higher) with the **"Desktop development with C++"** workload (C++20).
2. **CMake** (v3.20 or newer).
3. **Vulkan SDK** (1.3.x from [LunarG](https://vulkan.lunarg.com/)).
4. An **ASI Loader** for RDR2 (`dinput8.dll`). Download it only from the loader project's official release page and verify its checksum before installing.

#### B.2 Build the ASI Plugin
Run the following commands in PowerShell or Command Prompt:

```powershell
# Clone the repository
git clone https://github.com/ShreyasP10/Checkerboard-Rendering.git
cd Checkerboard-Rendering

# Create build directory and generate Visual Studio solution
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Compile the Release build
cmake --build . --config Release
```

The build process outputs:
- `build/bin/Release/rdr2-cbr.asi` — The compiled ASI mod binary.
- `build/bin/Release/cbr.ini` — The default configuration file.

### Step 3: Install into RDR2 from Scratch

#### 3.1 Locate Your RDR2 Installation Directory
Find the folder containing `RDR2.exe`:
- **Steam:** Right-click *Red Dead Redemption 2* in your Steam Library > **Manage** > **Browse local files** (typically `C:\Program Files (x86)\Steam\steamapps\common\Red Dead Redemption 2\`).
- **Rockstar Games Launcher:** Open Launcher > **Settings** > **My installed games** > **Red Dead Redemption 2** > **View installation folder** (typically `C:\Program Files\Rockstar Games\Red Dead Redemption 2\`).
- **Epic Games Store:** Open Epic Launcher > Library > Click the three dots under RDR2 > **Manage** > Click the folder icon next to *Installation* (typically `C:\Program Files\Epic Games\Red Dead Redemption 2\`).

#### 3.2 Install the ASI Loader
An ASI loader is required to load custom `.asi` game modifications:
1. Download `dinput8.dll` only from the loader project's official release page over HTTPS (e.g. the official [Script Hook RDR2](https://www.dev-c.com/rdr2/scripthookrdr2/) release by Alexander Blade). Do not use re-uploads, Discord attachments, or video-description links.
2. Verify integrity before installing: compare `Get-FileHash dinput8.dll -Algorithm SHA256` against the hash published on that release page (do not install if the page lists no hash or the hash mismatches). Scan with Windows Defender/SmartScreen.
3. Place the verified `dinput8.dll` directly into the RDR2 root folder (where `RDR2.exe` is located). Use strictly offline / story mode only; remove ASI loaders before joining Red Dead Online.

#### 3.3 Deploy CBR Plugin & Configuration
Copy the built mod files into your RDR2 root directory:
1. Copy `rdr2-cbr.asi` into the RDR2 root folder.
2. Copy `cbr.ini` into the RDR2 root folder.

Your RDR2 folder structure should look like this:
```
Red Dead Redemption 2/
├── RDR2.exe                  <-- Game executable
├── dinput8.dll               <-- ASI Loader (executes .asi plugins)
├── rdr2-cbr.asi              <-- Checkerboard Rendering Mod Plugin
├── cbr.ini                   <-- Mod configuration settings
└── ... (game data files)
```

---

### Step 4: Configure `cbr.ini` Before First Launch

Open `cbr.ini` in Notepad or your preferred text editor and customize the parameters for your monitor and hardware:

```ini
[General]
Enabled = true
TargetWidth = 3840           ; Set to your display resolution width (e.g. 3840 for 4K, 2560 for 1440p)
TargetHeight = 2160          ; Set to your display resolution height (e.g. 2160 for 4K, 1440 for 1440p)
PreferredApi = Vulkan        ; "Vulkan" (strongly recommended for Pascal/GTX 1070 Ti), "D3D12", or "Auto"
MipLodBias = -0.5            ; Reserved: not applied yet (will bias scene texture sampling once the geometry pass is hooked)

[Reconstruction]
DepthTolerance = 0.010       ; Relative depth disocclusion sensitivity threshold
EnableColorClamping = true   ; Enables variance clipping to eliminate temporal ghosting
ColorSpace = YCoCg           ; YCoCg provides artifact-free color bounding box calculation
HistoryWeight = 0.90         ; 0.90 retains 90% temporal history on static pixels
EnableSpatialFallback = true ; Uses cross-bilateral filter when history is disoccluded
EnableMotionDilation = true  ; 3x3 closest-depth motion dilation over active samples
DepthConvention = Reversed   ; RDR2 reversed-Z depth (1 near .. 0 far) or Standard (0 near .. 1 far)
DepthNear = 0.1              ; Near plane in metres (used for linear depth disocclusion tests)
DepthFar = 0.0               ; Far plane in metres (0.0 = infinite far plane for Reversed)

[Jitter]
JitterPattern = Checkerboard ; 2-phase complementary grid jitter (whole-pixel horizontal shift)
JitterScale = 1.0            ; DEPRECATED: parsed but intentionally ignored (coverage requires exactly 1 px)
JitterDirection = 1          ; Odd-frame horizontal shift direction: +1 or -1
ProjectionJitterSign = 1     ; Independent sign for projection matrix jitter offsets (proj[8]/proj[9])
JitterCompensation = 0.0     ; History reprojection jitter sign (default 0.0: whole-pixel shift is absorbed by sample mapping)

[Debug]
ShowOverlay = false          ; Toggle in-game overlay
DebugView = 0                ; 0=Reconstructed 4K, 1=CBR Mask, 2=Disocclusion Heatmap, 3=Motion Vectors, 4=Raw, 5=Spatial Baseline
LogToFile = true             ; Writes cbr.log for installation diagnostic
LogLevel = Info              ; Debug, Info, Warning, Error
```

---

### Step 5: Recommended In-Game Graphics Settings (Complete Walkthrough)

> **Note:** these are the *intended* settings for when integration lands. They have not been verified in-game, and claims about engine behavior (velocity vectors, MSAA, VRAM headroom) are working assumptions from the design, to be confirmed by reverse engineering.

Launch *Red Dead Redemption 2*, open **Settings > Graphics**, and configure the options as detailed below:

#### 1. Display & Window Settings
* **Screen Type:** Set to **Fullscreen** (Windowed/Borderless can cause DWM scaling artifacts and input latency).
* **Resolution:** Set to your target output resolution (e.g., **3840×2160** or **2560×1440**). *Must match `TargetWidth` and `TargetHeight` in `cbr.ini`.*
* **Refresh Rate:** Set to your monitor's native refresh rate (e.g. 60Hz, 120Hz, 144Hz).
* **V-Sync:** **On** (or use NVIDIA G-Sync / AMD FreeSync) to prevent presentation screen tearing.
* **Triple Buffering:** **On** (smooths frame delivery and pacing when targeting 60 FPS).

#### 2. Advanced Graphics API Setting (Critical!)
* **Unlock Advanced Settings:** Set to **Unlocked**.
* **Graphics API:** Set to **Vulkan** (the planned primary target for Pascal GP104 hardware; the expected benefit is finer control over sample positions, to be confirmed. If switching from DirectX 12 to Vulkan, restart the game).
* **Async Compute:** **On** (untested with this mod).

#### 3. Anti-Aliasing & Resolution Scaling (Critical!)
* **Resolution Scale:** Set to **Off / 1.0×** (*CRITICAL:* Never set this to 0.75×, 0.85×, etc. In-game resolution scaling breaks 1:1 subpixel checkerboard parity mapping).
* **TAA (Temporal Anti-Aliasing):** Set to **Medium** or **High** (*Assumption to verify:* the engine's velocity vectors (`u_Velocity`) are expected to be produced only while TAA is enabled, and CBR needs them for temporal history reprojection).
* **TAA Sharpening:** Adjust according to personal preference (typically 30%–50%).
* **FXAA:** **Off** (redundant post-processing blur).
* **MSAA:** **Off** (leave in-game MSAA disabled; the planned design has CBR allocate its own dedicated 2× MSAA intermediate buffer, which is not implemented yet).

#### 4. Geometry & Texture Settings (Optimized for GTX 1070 Ti / Pascal 8 GB)
* **Texture Quality:** **Ultra** (the CBR buffers themselves need ~300.58 MiB (315.19 MB); whether Ultra textures plus those fit in 8 GB at 4K is unverified, so lower this first if you run out of VRAM).
* **Anisotropic Filtering:** **16×** (negligible performance cost on Pascal GPUs; keeps road and terrain textures sharp at oblique viewing angles).
* **Lighting Quality:** **Medium** or **High**.
* **Global Illumination Quality:** **High**.
* **Shadow Quality:** **High**.
* **Far Shadow Quality:** **Medium** or **High**.
* **Screen Space Ambient Occlusion (SSAO):** **High**.
* **Reflection Quality:** **Medium** (High/Ultra reflections are very expensive in RDR2).
* **Mirror Quality:** **High**.
* **Water Quality:** **Medium** (Custom / Water Physics: 2/4).
* **Volumetrics Quality:** **Medium** (Raymarched volumetric fog is compute-heavy at 4K; Medium provides optimal 60 FPS headroom).
* **Particle Quality:** **Medium**.
* **Tessellation Quality:** **High** (keeps tree bark and ground snow tracks detailed).
* **Motion Blur:** **Off** (recommended for cleanest checkerboard temporal stability).

---

### Step 6: Verifying Installation via Logs & Debug Modes

#### 6.1 Check Initialization Log (`cbr.log`)
Exit or Alt-Tab from the game and open `cbr.log` in the RDR2 root folder to verify that the mod was loaded:
```text
=================================================================
 RDR2 Checkerboard Rendering Mod (CBR) Log Initialized           
 Maintainer: Shreyas Pawar                                       
 Target: NVIDIA GeForce GTX 1070 Ti & Vulkan / DX12              
=================================================================
[INFO] Initializing CBREngine for Red Dead Redemption 2...
[INFO] Configuration loaded from cbr.ini (Target: 3840x2160, API: Vulkan, CBR Enabled: true)
[INFO] RenderTargetManager initialized for target: 3840x2160
[INFO] Quarter-Resolution 2x MSAA Buffer size: 1920x1080
[INFO] Total CBR VRAM Footprint: 300.58 MiB (315.19 MB)
[WARN] Vulkan hook installation is not implemented yet; no hooks are active.
[INFO] CBREngine initialized successfully. Ready for frame interception.
```

#### 6.2 Visual Inspection with Debug Views
You can toggle diagnostic visualization modes in `cbr.ini` by modifying `DebugView`:
- `DebugView = 0`: **Normal Reconstructed Output** (standard 4K CBR presentation).
- `DebugView = 1`: **Checkerboard Subpixel Mask** — displays a 1:1 pixel mask. **White** pixels were natively shaded in the current frame; **Black** pixels were reconstructed from history.
- `DebugView = 2`: **Disocclusion Heatmap** — **Green** indicates valid temporal reprojection; **Red** highlights disoccluded geometry using spatial cross-bilateral fallback.
- `DebugView = 3`: **Motion Vector Field** — visualizes screen-space velocity vectors (R=horizontal motion, G=vertical motion).
- `DebugView = 4`: **Raw Buffer** — displays the unresolved quarter-resolution native render.
- `DebugView = 5`: **Spatial Baseline Resolve** — displays naive spatial reconstruction for A/B quality comparison against temporal CBR.

---

### Step 7: Troubleshooting & FAQ

* **Q: The game crashes immediately on startup.**
  * *A:* Verify you installed a clean, compatible `dinput8.dll` ASI loader. The plugin is built with a static CRT, so no Visual C++ redistributable is required for it. Check `cbr.log` for any error messages.
* **Q: `cbr.log` is not created at all.**
  * *A:* This means `dinput8.dll` is either missing, blocked by Windows SmartScreen/Antivirus, or located in the wrong directory. Ensure `dinput8.dll`, `rdr2-cbr.asi`, and `cbr.ini` are in the **same folder** as `RDR2.exe`.
* **Q: The game looks blurry or pixelated.**
  * *A:* Verify that **Resolution Scale** in the Graphics menu is set to **Off / 1.0×**, and that **TAA** is set to **Medium** or **High**.
* **Q: Can I use this mod in Red Dead Online?**
  * *A:* **No.** This mod is strictly designed and intended for single-player story mode. Never use modified game files or ASI loaders when connecting to Red Dead Online to avoid anti-cheat bans.

---

## Configuration (`cbr.ini`)

Settings are customized in `cbr.ini` before launch (live editing via the in-game overlay is planned). `PreferredApi` accepts `Vulkan`, `D3D12` or `Auto`. `JitterPattern = Halton` is reserved and currently falls back to Checkerboard with a warning.

```ini
[General]
Enabled = true
TargetWidth = 3840
TargetHeight = 2160
PreferredApi = Vulkan
MipLodBias = -0.5

[Reconstruction]
DepthTolerance = 0.010
EnableColorClamping = true
ColorSpace = YCoCg
HistoryWeight = 0.90
EnableSpatialFallback = true
EnableMotionDilation = true

[Jitter]
JitterPattern = Checkerboard
JitterScale = 1.0            ; DEPRECATED: parsed but intentionally ignored
JitterDirection = 1
ProjectionJitterSign = 1
JitterCompensation = 0.0
```

---

## Testing

```bash
cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON
cmake --build build-tests --target cbr_tests cbr_engine_tests
ctest --test-dir build-tests --output-on-failure
python tests/check_shader_mapping.py
```

The tests cover the portable code only (no Windows APIs or GPU): config, logger, jitter, push-constant layout, and the engine's hook-retry, once-per-frame dispatch and frame-parity logic (using fake hook installers). Validate shaders with (mirrors CI):

```bash
glslangValidator -V shaders/cbr_reconstruct.comp -o /tmp/r.spv
glslangValidator -V shaders/cbr_resolve_simple.comp -o /tmp/s.spv
glslangValidator -D -e CSMain -S comp -V shaders/cbr_reconstruct.hlsl -o /tmp/h.spv
# plus, when installed: dxc -T cs_6_0 -E CSMain shaders/cbr_reconstruct.hlsl -Fo /tmp/r.dxil
```

## Changelog / Recent Updates (v0.1.0-alpha)

* **Thread-safety:** `ConfigManager` no longer exposes an unprotected reference (`GetMutableConfig` removed; mutate via `Modify`/`UpdateConfig`). `RenderTargetManager` guards dimensions behind `shared_mutex` (atomic VRAM counter); `JitterManager` guards jitter state behind `mutex`; `ReconstructionPass::m_isVulkan` is atomic. `ConfigManager::Load/Save` do file I/O outside the lock and batch warnings.
* **Shader hardening (NaN/Inf guards):** `cbr_reconstruct.comp`/`.hlsl` reject non-finite device depth, velocity, UVs, and linear depths as disoccluded (previously a `NaN` compared `false` and poisoned ping-pong history). Documented sampler requirements (history color/velocity `LINEAR_CLAMP`, history depth point fetch).
* **Configuration (`cbr.ini`) hardening:** 64 KiB / 500-line / 1024-char caps, strict integer parsing (rejects trailing garbage), `LogLevel` sanitized (length cap, CR/LF stripped), unknown-key warnings, even-dimension handling unified via `MakeEvenUp`, `Save` verifies `flush/good`, logs use the filename only (no absolute-path PII). `JitterScale` is parsed but intentionally ignored (coverage requires exactly 1 px); `JitterPattern = Halton` always round-trips to `Checkerboard`.
* **Loader safety:** `DllMain` now checks the module-pin result and documents that `CreateThread` under the loader lock is a fallback only (preferred path is the loader calling `CBR_PluginInit`).
* **Reliability:** `DispatchVulkan/DX12` warn once (not every frame) when uninitialized; `RenderTargetManager::Initialize` rejects `< 2` / `> 16384`; `BuildPushConstants` guards zero extents; Vulkan mini-structs use `memcpy` copy-out with bounds checks.
* **CI:** least-privilege permissions (read-only except release job), SHA-pinned actions (`checkout` v4.2.2, `upload-artifact` v4.6.2, `gh-release` v2.6.2), HLSL gated by the glslang front-end plus `dxc` when installed (failures no longer swallowed).
* **Source of truth:** the standalone tree is canonical. Canonical limits live in `include/cbr/config.h`; `include/cbr/limits.h` is an optional backwards-compat shim (not referenced by `CMakeLists.txt`). `CODEBASE.md` is a point-in-time snapshot and may lag the tree.

---

## Contributing

Contributions, feedback, and research findings are welcome! Please check [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines, focus areas, and code standards.

---

## Collaborators & Maintainers

- **Shreyas Pawar** – Project Lead, Co-Owner & Graphics Architecture

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Disclaimer

This mod is for **educational, experimental, and research purposes only**. It is not affiliated with, endorsed by, or associated with Rockstar Games or Take-Two Interactive. Use strictly in offline single-player mode.
