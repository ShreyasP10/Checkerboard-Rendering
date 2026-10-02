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
- [Complete Source Codebase (Single Document)](CODEBASE.md)
- [Engineering Documentation](#engineering-documentation)
- [Reconstruction Shader Math](#reconstruction-shader-math)
- [Hardware & Development Requirements](#hardware--development-requirements)
- [How to Build, Install & Run](#-how-to-build-install--run)
- [Configuration](#configuration)
- [Testing](#testing)
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
- [x] Temporal reprojection math with velocity vector sampling.
- [ ] Camera depth unprojection fallback (not implemented).
- [x] Depth delta disocclusion detection with spatial cross-bilateral filter fallback.
- [x] 3×3 neighborhood color bounding box clamping (YCoCg or RGB, selectable via `ColorSpace`) to suppress ghosting.
- [x] Reconstruction pass writes a full-resolution history-depth target for the next frame's disocclusion test.
- [x] Host-side unit tests (config, logger, jitter, VRAM accounting) and a CI workflow; shaders are validated with glslang.
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

The shader rejects the history sample and executes a spatial cross-bilateral filter from the current frame's four orthogonal (cardinal: up, down, left, right) active samples:

$$C_{\text{spatial}} = \frac{\sum_{k=1}^4 w_k C_k}{\sum_{k=1}^4 w_k}, \quad w_k = \exp\left(-\frac{\|p_k - p\|^2}{2\sigma_d^2}\right) \cdot \exp\left(-\frac{|Z_k - Z|^2}{2\sigma_z^2}\right)$$

---

## Hardware & Development Requirements

| Component | Minimum Specification | Recommended (Target Baseline) | Role in CBR Pipeline |
|---|---|---|---|
| **GPU** | GTX 1060 (6 GB) / RX 580 (8 GB) | **NVIDIA GeForce GTX 1070 Ti (8 GB GDDR5)** | Compute shader capability (SM 5.0+), 2× MSAA rasterization, ≥250 GB/s bandwidth |
| **GPU VRAM** | 6 GB | **8 GB GDDR5** | Accommodates ~268.95 MB dedicated VRAM for 4K ping-pong history and depth buffers |
| **CPU** | Quad-Core (i5-8400 / Ryzen 2600) | **6-Core / 12-Thread (i7 / Ryzen 3600+)** | Hook overhead to be measured once live hooks exist |
| **RAM** | 12 GB | **16 GB DDR4 Dual-Channel** | System memory stability during texture streaming |
| **OS** | Windows 10 (64-bit, 19041+) | **Windows 10 / Windows 11 (64-bit)** | Native Vulkan 1.3 and DirectX 12 support |
| **Display** | 1080p (with DSR 4K) | **Native 1440p or 4K (3840×2160) Monitor / TV** | Presentation resolution for reconstructed output |

---

<a id="building"></a>
## 🚀 How to Build, Install & Run

### Step 1: Software Prerequisites
To compile the mod from source, ensure you have:
1. **Visual Studio 2022** (Community or higher) with the **"Desktop development with C++"** workload (C++20).
2. **CMake** (v3.20 or newer).
3. **Vulkan SDK** (1.3.x from [LunarG](https://vulkan.lunarg.com/)).
4. An **ASI Loader** for RDR2 (`dinput8.dll`). Download it only from the loader project's official release page and verify its checksum before installing.

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

### Step 3: Install into RDR2 from Scratch

#### 3.1 Locate Your RDR2 Installation Directory
Find the folder containing `RDR2.exe`:
- **Steam:** Right-click *Red Dead Redemption 2* in your Steam Library > **Manage** > **Browse local files** (typically `C:\Program Files (x86)\Steam\steamapps\common\Red Dead Redemption 2\`).
- **Rockstar Games Launcher:** Open Launcher > **Settings** > **My installed games** > **Red Dead Redemption 2** > **View installation folder** (typically `C:\Program Files\Rockstar Games\Red Dead Redemption 2\`).
- **Epic Games Store:** Open Epic Launcher > Library > Click the three dots under RDR2 > **Manage** > Click the folder icon next to *Installation* (typically `C:\Program Files\Epic Games\Red Dead Redemption 2\`).

#### 3.2 Install the ASI Loader
An ASI loader is required to load custom `.asi` game modifications:
1. Download `dinput8.dll` from the official [Script Hook RDR2](http://www.dev-c.com/rdr2/scripthookrdr2/) release by Alexander Blade or an authenticated open-source ASI loader.
2. Place `dinput8.dll` directly into the RDR2 root folder (where `RDR2.exe` is located).

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
MipLodBias = -0.5            ; -0.5 preserves full 4K texture sharpness on 1080p geometry

[Reconstruction]
DepthTolerance = 0.010       ; Relative depth disocclusion sensitivity threshold
EnableColorClamping = true   ; Enables variance clipping to eliminate temporal ghosting
ColorSpace = YCoCg           ; YCoCg provides artifact-free color bounding box calculation
HistoryWeight = 0.90         ; 0.90 retains 90% temporal history on static pixels
EnableSpatialFallback = true ; Uses cross-bilateral filter when history is disoccluded

[Jitter]
JitterPattern = Checkerboard ; 2-phase subpixel complementary grid jitter
JitterScale = 1.0            ; 1.0 = exact 0.5-pixel subpixel perturbation

[Debug]
ShowOverlay = false          ; Toggle in-game overlay
DebugView = 0                ; 0=Reconstructed 4K, 1=CBR Mask, 2=Disocclusion Heatmap, 3=Motion Vectors, 4=Raw
LogToFile = true             ; Writes cbr.log for installation diagnostic
LogLevel = Info              ; Debug, Info, Warning, Error
```

---

### Step 5: Recommended In-Game Graphics Settings (Complete Walkthrough)

Launch *Red Dead Redemption 2*, open **Settings > Graphics**, and configure the options as detailed below:

#### 1. Display & Window Settings
* **Screen Type:** Set to **Fullscreen** (Windowed/Borderless can cause DWM scaling artifacts and input latency).
* **Resolution:** Set to your target output resolution (e.g., **3840×2160** or **2560×1440**). *Must match `TargetWidth` and `TargetHeight` in `cbr.ini`.*
* **Refresh Rate:** Set to your monitor's native refresh rate (e.g. 60Hz, 120Hz, 144Hz).
* **V-Sync:** **On** (or use NVIDIA G-Sync / AMD FreeSync) to prevent presentation screen tearing.
* **Triple Buffering:** **On** (smooths frame delivery and pacing when targeting 60 FPS).

#### 2. Advanced Graphics API Setting (Critical!)
* **Unlock Advanced Settings:** Set to **Unlocked**.
* **Graphics API:** Set to **Vulkan** (*Crucial:* Vulkan offers direct subpixel sample control and lower CPU overhead on Pascal GP104 hardware. If switching from DirectX 12 to Vulkan, restart the game).
* **Async Compute:** **On** (enables concurrent execution of compute reconstruction alongside rasterization passes).

#### 3. Anti-Aliasing & Resolution Scaling (Critical!)
* **Resolution Scale:** Set to **Off / 1.0×** (*CRITICAL:* Never set this to 0.75×, 0.85×, etc. In-game resolution scaling breaks 1:1 subpixel checkerboard parity mapping).
* **TAA (Temporal Anti-Aliasing):** Set to **Medium** or **High** (*CRITICAL:* RDR2's internal motion/velocity vectors `u_Velocity` are only generated by the RAGE engine when TAA is enabled. CBR requires these vectors for temporal history reprojection).
* **TAA Sharpening:** Adjust according to personal preference (typically 30%–50%).
* **FXAA:** **Off** (redundant post-processing blur).
* **MSAA:** **Off** (*CRITICAL:* Leave in-game MSAA disabled. CBR allocates its own dedicated 2× MSAA intermediate buffer).

#### 4. Geometry & Texture Settings (Optimized for GTX 1070 Ti / Pascal 8 GB)
* **Texture Quality:** **Ultra** (Textures sample at full 4K Nyquist resolution due to CBR's `-0.5` `MipLodBias`; fits comfortably in 8 GB VRAM with ~269 MB CBR overhead).
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
[INFO] Configuration successfully loaded from cbr.ini (Target: 3840x2160, API: Vulkan, CBR Enabled: true)
[INFO] RenderTargetManager initialized for target: 3840x2160
[INFO] Quarter-Resolution 2x MSAA Buffer size: 1920x1080
[INFO] Total CBR VRAM Footprint: 268.95 MB
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

---

### Step 7: Troubleshooting & FAQ

* **Q: The game crashes immediately on startup.**
  * *A:* Verify you installed a clean, compatible `dinput8.dll` ASI loader. Ensure Microsoft Visual C++ 2015–2022 Redistributable (x64) is installed. Check `cbr.log` for any error messages.
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
```

---

## Testing

```bash
cmake -S . -B build-tests -DCBR_BUILD_TESTS=ON
cmake --build build-tests --target cbr_tests
ctest --test-dir build-tests --output-on-failure
```

The tests cover the portable code only (no Windows APIs or GPU). Validate shaders with:

```bash
glslangValidator -V shaders/cbr_reconstruct.comp -o /tmp/r.spv
glslangValidator -V shaders/cbr_resolve_simple.comp -o /tmp/s.spv
glslangValidator -D -e CSMain -S comp -V shaders/cbr_reconstruct.hlsl -o /tmp/h.spv
```

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
