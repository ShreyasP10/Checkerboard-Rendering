# RDR2 Checkerboard Rendering Mod (CBR)

[![Status: Planning & Prototyping](https://img.shields.io/badge/status-prototyping-yellow.svg)]()
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-blue.svg)]()
[![API: Vulkan / DX12](https://img.shields.io/badge/API-Vulkan%20%7C%20DX12-green.svg)]()
[![Target: GTX 1070 Ti](https://img.shields.io/badge/Target-GTX%201070%20Ti%20(Pascal)-orange.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

A community-driven graphics modification implementing **Checkerboard Rendering (CBR)** in *Red Dead Redemption 2* (PC), bringing the PlayStation 4 Pro's hardware-assisted temporal reconstruction technique to modern PC GPUs—specifically targeting the **NVIDIA GeForce GTX 1070 Ti** and Pascal-architecture hardware.

> **⚠️ Project Status: Prototyping & Pipeline Architecture Phase**  
> We have reverse-engineered the rendering pipeline concepts, completed the engineering specifications, authored the reconstruction compute shaders, and established the Vulkan/DX12 hook layer. Public release builds will be packaged following game integration validation.

---

## 📖 Table of Contents
- [About](#about)
- [How It Works](#how-it-works)
- [Key Features](#key-features)
- [Architecture & Repository Structure](#architecture--repository-structure)
- [Engineering Documentation](#engineering-documentation)
- [Reconstruction Shader Math](#reconstruction-shader-math)
- [Hardware & Development Requirements](#hardware--development-requirements)
- [Building](#building)
- [Configuration](#configuration)
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
2. **Subpixel Projection Jitter:** Alternates the camera projection matrix by $\pm 0.5$ pixels every frame, shifting the 2× MSAA subpixel grid to cover complementary checkerboard coordinates.
3. **MIP LOD Bias Injection:** Injects a $-0.5$ LOD bias into scene texture samplers, ensuring high-frequency textures sample at full 4K Nyquist clarity despite reduced geometry resolution.
4. **Compute Shader Reconstruction:** Runs a compute shader (`cbr_reconstruct.comp` / `cbr_reconstruct.hlsl`) that evaluates pixel parity, reprojects history using motion vectors, tests depth for disocclusion, and clamps against the 3×3 color neighborhood.

---

## Key Features

- [x] Full architectural specification & requirements documentation (PRD, SRD, SRS, TRD, DEV_PLAN, RISK_REGISTER).
- [x] Complete GLSL & HLSL reconstruction compute shaders with 2× MSAA unpack and subpixel parity testing.
- [x] Temporal reprojection with velocity vector sampling and camera depth unprojection fallback.
- [x] Depth delta disocclusion detection with spatial cross-bilateral filter fallback.
- [x] 3×3 neighborhood color bounding box clamping in YCoCg space to suppress ghosting.
- [x] Pascal architecture optimization (GP104 warp size 32, shared memory tiling, low register pressure).
- [x] MinHook-powered Vulkan & DirectX 12 interception layer.
- [x] Runtime configuration via `cbr.ini` and in-game ImGui debug overlay.
- [x] Multi-mode debug visualizer (checkerboard grid mask, disocclusion heatmap, motion vector field).

---

## Architecture & Repository Structure

```
Checkerboard-Rendering/
├── CMakeLists.txt              # CMake build script for rdr2-cbr.asi
├── LICENSE                     # MIT License
├── README.md                   # Project overview and instructions
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
│   ├── config.h                # cbr.ini configuration reader & settings
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

---

## Reconstruction Shader Math

The core compute shader resolves pixels based on parity:

$$\text{Phase}(x, y) = (x + y) \pmod 2$$

- When $\text{Phase}(x, y) = (\text{FrameIndex} \pmod 2)$, the pixel is sampled directly from the current frame's 2× MSAA buffer.
- When $\text{Phase}(x, y) \neq (\text{FrameIndex} \pmod 2)$, the pixel is reprojected from history:

$$\mathbf{UV}_{\text{prev}} = \mathbf{UV}_{\text{curr}} - \mathbf{V}(x, y)$$

If the depth variance exceeds the tolerance threshold:

$$\Delta Z = \frac{|Z_{\text{curr}} - Z_{\text{prev}}|}{\max(Z_{\text{curr}}, 10^{-5})} > \text{Threshold}$$

The shader rejects the history sample and executes a spatial cross-bilateral filter from the current frame's four diagonally adjacent active samples:

$$C_{\text{spatial}} = \frac{\sum_{k=1}^4 w_k C_k}{\sum_{k=1}^4 w_k}, \quad w_k = \exp\left(-\frac{\|p_k - p\|^2}{2\sigma_d^2}\right) \cdot \exp\left(-\frac{|Z_k - Z|^2}{2\sigma_z^2}\right)$$

---

## Hardware & Development Requirements

| Component | Minimum Specification | Recommended (Target Baseline) | Role in CBR Pipeline |
|---|---|---|---|
| **GPU** | GTX 1060 (6 GB) / RX 580 (8 GB) | **NVIDIA GeForce GTX 1070 Ti (8 GB GDDR5)** | Compute shader capability (SM 5.0+), 2× MSAA rasterization, ≥250 GB/s bandwidth |
| **GPU VRAM** | 6 GB | **8 GB GDDR5** | Accommodates ~285 MB dedicated VRAM for 4K ping-pong history and depth buffers |
| **CPU** | Quad-Core (i5-8400 / Ryzen 2600) | **6-Core / 12-Thread (i7 / Ryzen 3600+)** | Interception hooks add minimal overhead ($\le 0.05\,\mu\text{s}$ per draw call) |
| **RAM** | 12 GB | **16 GB DDR4 Dual-Channel** | System memory stability during texture streaming |
| **OS** | Windows 10 (64-bit, 19041+) | **Windows 10 / Windows 11 (64-bit)** | Native Vulkan 1.3 and DirectX 12 support |
| **Display** | 1080p (with DSR 4K) | **Native 1440p or 4K (3840×2160) Monitor / TV** | Presentation resolution for reconstructed output |

---

## 🚀 How to Build, Install & Run

### Step 1: Software Prerequisites
To compile the mod from source, ensure you have:
1. **Visual Studio 2022** (Community or higher) with the **"Desktop development with C++"** workload (C++20).
2. **CMake** (v3.20 or newer).
3. **Vulkan SDK** (1.3.x from [LunarG](https://vulkan.lunarg.com/)).
4. An **ASI Loader** for RDR2, such as `dinput8.dll` (from ScriptHookRDR2 or open-source ASI loaders).

### Step 2: Build the ASI Plugin
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
- `build/bin/rdr2-cbr.asi` — The compiled ASI mod binary.
- `build/bin/cbr.ini` — The default configuration file.

### Step 3: Install into RDR2
1. Locate your *Red Dead Redemption 2* game root directory (where `RDR2.exe` is found):
   - **Steam:** `Steam\steamapps\common\Red Dead Redemption 2\`
   - **Rockstar Games:** `Rockstar Games\Red Dead Redemption 2\`
   - **Epic Games:** `Epic Games\Red Dead Redemption 2\`
2. Copy the following files into the RDR2 root folder:
   - `dinput8.dll` (ASI Loader)
   - `rdr2-cbr.asi` (from `build/bin/`)
   - `cbr.ini` (from `build/bin/`)

```
Red Dead Redemption 2/
├── RDR2.exe
├── dinput8.dll         <-- ASI Loader (executes .asi plugins)
├── rdr2-cbr.asi        <-- CBR Mod Plugin
└── cbr.ini             <-- CBR Configuration Settings
```

### Step 4: Recommended In-Game Settings
Launch *Red Dead Redemption 2*, open **Settings > Graphics**, and configure:
1. **Graphics API:** Set to **Vulkan** (Vulkan allows Pascal GPUs direct subpixel sample control).
2. **Resolution:** Set to **3840×2160 (4K)** (or your target display resolution).
3. **TAA (Temporal Anti-Aliasing):** Set to **Medium** or **High** (ensures internal velocity vectors are generated).
4. **Resolution Scale:** Set to **Off / 1.0×** (the mod automatically handles quarter-resolution rendering and resolve).

### Step 5: In-Game Controls & Debug Modes
- **Toggle Overlay:** Press **`F11`** or **`Insert`** in-game to display the ImGui control panel.
- **Debug Views (configurable in `cbr.ini` or ImGui):**
  - `DebugView = 0`: Normal CBR Reconstructed 4K output.
  - `DebugView = 1`: **Checkerboard Mask** — reveals active frame samples vs reconstructed pixels.
  - `DebugView = 2`: **Disocclusion Heatmap** — **Green** indicates valid temporal history; **Red** highlights disoccluded geometry using spatial fallback.
  - `DebugView = 3`: **Motion Vector Field** — visualizes screen-space velocity vectors.
  - `DebugView = 4`: **Raw Buffer** — displays quarter-resolution unresolved image.

### Step 6: Verifying Installation via Logs
Upon launching the game, open `cbr.log` in the RDR2 root directory to verify hook initialization:
```text
=================================================================
 RDR2 Checkerboard Rendering Mod (CBR) Log Initialized           
 Maintainer: Shreyas Pawar                                       
 Target: NVIDIA GeForce GTX 1070 Ti & Vulkan / DX12              
=================================================================
[INFO] Initializing CBREngine for Red Dead Redemption 2...
[INFO] Configuration successfully loaded from cbr.ini (Target: 3840x2160, API: Vulkan, CBR Enabled: true)
[INFO] RenderTargetManager initialized for target: 3840x2160
[INFO] Quarter-Resolution 2x MSAA Buffer size: 1920x1080
[INFO] Total CBR VRAM Footprint: 285.20 MB
[INFO] Vulkan interception hooks successfully registered.
[INFO] CBREngine initialized successfully. Ready for frame interception.
```

---

## Configuration (`cbr.ini`)

Settings can be customized before launch in `cbr.ini` or on the fly via the in-game overlay:

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
```

---

## Contributing

Contributions, feedback, and research findings are welcome! Please check [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines, focus areas, and code standards.

---

## Collaborators & Maintainers

- **Atharva Mahajan** – Project Lead, Co-Owner & Graphics Architecture
- **Shreyas Pawar** – Co-Owner & Graphics Architecture

---

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.

---

## Disclaimer

This mod is for **educational, experimental, and research purposes only**. It is not affiliated with, endorsed by, or associated with Rockstar Games or Take-Two Interactive. Use strictly in offline single-player mode.
