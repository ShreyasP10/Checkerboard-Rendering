# RAGE Engine Generational Architecture & RDR2 Vulkan Hooking Analysis

This document provides a comprehensive technical reference for the internal architecture of the **Rockstar Advanced Game Engine (RAGE)**, tracking its generational evolution from RAGE 1 through RAGE 9, with deep focus on **RAGE 8** (Red Dead Redemption 2) and how its low-level Vulkan rendering pipeline is intercepted by `rdr2-cbr.asi`.

---

## 1. Generational Evolution of the RAGE Engine

While Rockstar's internal **RAGE Technology Group** (Rockstar San Diego, formerly Angel Studios) maintains the engine as a continuous, per-project branched C++ codebase (`rage::grcDevice`, `rage::fwScene`, `rage::phSimulator`) without external version branding, the graphics reverse-engineering community tracks major architectural rewrites across generations:

| Generation & Flagship Titles | Graphics APIs | Core Rendering Pipeline | Anti-Aliasing & Upscaling | Lighting, Volumetrics & Shadows | Signature Engine Leap |
|---|---|---|---|---|---|
| **RAGE 1–4: Early Era**<br>*Table Tennis (2006)*<br>*GTA IV (2008)*<br>*Midnight Club: LA (2008)* | DirectX 9.0c, Xbox 360 Xenos, PS3 LibGCM | Early Deferred Shading (3 MRT G-Buffers: Albedo, Normal+Specular, Stencil/AO); Standard $[0..1]$ Z-buffer | None (stippled alpha / hardware MSAA on 360; 640p upscale on PS3) | Radiosity-baked vertex/lightmap GI; Cascaded Shadow Maps (CSM) with stippled dither fades | Replaced Criterion's RenderWare; integrated Euphoria procedural ragdoll & Bullet physics |
| **RAGE 5–6: Late 7th Gen**<br>*Red Dead Redemption (2010)*<br>*Max Payne 3 (2012)* | DirectX 9 / DirectX 11 (MP3), PS3, Xbox 360 | Hybrid Deferred + Forward pass; hardware tessellation introduced in MP3 DX11 | MSAA + FXAA; hardware 2× MSAA on Xbox 360 | Dynamic time-of-day atmospheric scattering; Screen-Space Ambient Occlusion (SSAO) | Seamless open-world terrain streaming, skeletal muscle deformation, dynamic weather transitions |
| **RAGE 7: 8th Gen Transition**<br>*Grand Theft Auto V (2013 / 2015 PC)* | DirectX 10 / 10.1 / DirectX 11 | Multi-Pass Hybrid Deferred (4 MRT G-Buffers + Reversed-Z logarithmic depth D32S8 + Material ID Stencil) | FXAA, 2×–8× MSAA, NVIDIA TXAA (2025 Enhanced PC update added DLSS/FSR) | Low-poly 6-face dynamic camera cubemap for reflections; 4-cascade CSM; filmic tonemapping | Stencil-tagged mesh categories (`0x89` player, `0x82` vehicle, `0x03` foliage); compute-based auto-exposure |
| **RAGE 8: Modern Low-Level Era**<br>*Red Dead Redemption 2 (2018 Console / 2019 PC)* | Vulkan 1.3, DirectX 12, PS4 GNM, Xbox One | Full Physically Based Rendering (PBR) 6-target G-Buffer + Reversed-Z `D32_SFLOAT_S8_UINT` + Async Compute | PS4 Pro Hardware 2× MSAA Checkerboard Rendering (CBR), TAA, DLSS, FSR 2 | Voxel/raymarched volumetric clouds & fog, split-sum IBL cubemap convolution, 2048×2048 R16_UNORM mud/snow POM | Built from ground up for explicit low-level command buffers (`VkCommandBuffer` / `ID3D12GraphicsCommandList`) and temporal accumulation |
| **RAGE 9: Current-Gen Ray-Traced Era**<br>*GTA V Enhanced (2025 testbed)*<br>*Grand Theft Auto VI* | Native DirectX 12 Ultimate, Vulkan, PS5 GNM2 | GPU-Driven Rendering (meshlet culling / indirect draws) + Hybrid Ray-Traced Deferred PBR | Native temporal reconstruction (FSR / PSSR / DLSS) with subpixel motion vectors | Real-Time Ray-Traced Global Illumination (RTGI), RT reflections, RT ambient occlusion (RTAO) | Real-time physically simulated 3D water grids, strand-based hair/fur, multi-bounce per-pixel GI |

---

## 2. Low-Level RAGE 8 Engine Mechanics Intercepted by CBR

### A. Reversed-Z Infinite Far Plane
- **GTA IV (RAGE 4)** used a standard $[0.0\,\text{near} \dots 1.0\,\text{far}]$ depth buffer, which suffered from distant Z-fighting on buildings when flying helicopters at high altitude.
- **RAGE 7 & 8 (GTA V & RDR2)** switched to a **Reversed-Z** ($1.0\,\text{near} \dots 0.0\,\text{far}$) floating-point depth buffer (`VK_FORMAT_D32_SFLOAT_S8_UINT` = 130) with an infinite far plane ($z_{\text{far}} = 0.0$) and a near clip plane around $0.10\,\text{m}$–$0.15\,\text{m}$.
- Because IEEE-754 32-bit floating-point numbers have exponentially higher mantissa density near $0.0$, Reversed-Z allocates massive precision to distant geometry, completely eliminating Z-fighting across RDR2's 10+ kilometer landscapes.
- **CBR Mod Implementation**: In `shaders/cbr_reconstruct.comp`, the depth linearization function handles this directly:
  ```glsl
  float LinearizeDepth(float d) {
      if (pc.u_ZFar == 0.0) {
          // Reversed-Z infinite far plane: zNear / max(d, 1e-7)
          return pc.u_ZNear / max(d, 1e-7);
      }
      return (pc.u_ZNear * pc.u_ZFar) / (pc.u_ZFar + d * (pc.u_ZNear - pc.u_ZFar));
  }
  ```
  And `include/cbr/depth_convention.h` sets `DepthConvention::Reversed`, ensuring bilateral depth weighting and disocclusion detection operate in true linear metric space.

### B. 8-Bit Stencil Channel (`D32_SFLOAT_S8_UINT`) as a Material Router
In RDR2, RAGE never binds a standalone depth format (`D32_SFLOAT`) for the main scene; it binds `VK_FORMAT_D32_SFLOAT_S8_UINT` (format 130). During the G-buffer pass, every rendered mesh writes an 8-bit category identifier into the stencil byte:
- `0x89`: Arthur Morgan / John Marston (activates human skin subsurface scattering and high-detail contact shadow filters)
- `0x82`: Mount (horse, carriage, or train; triggers anisotropic fur sheen and mud splatter)
- `0x01`: NPCs and wildlife
- `0x03`: Tree foliage, shrubs, and grass (triggers two-sided leaf transmission lighting)
- `0x07`: Sky dome and distant atmospheric background

Downstream lighting passes (such as local lantern light cones, foliage lighting, and skin SSS) read this stencil byte via stencil test masking to apply material-specific shaders without branching.
- **CBR Mod Implementation**: In `src/hooks_vulkan.cpp`, copying depth into `quarterDepth` strictly isolates `VK_IMAGE_ASPECT_DEPTH_BIT` (`2u`) without requesting `VK_IMAGE_ASPECT_STENCIL_BIT` (`4u`):
  ```cpp
  const uint32_t srcDepthAspect = (depthAspect & 2u) ? 2u : depthAspect;
  const uint32_t dstDepthAspect = (res.quarterDepth.aspectMask & 2u) ? 2u : res.quarterDepth.aspectMask;
  ```
  This satisfies Vulkan validation rule `VUID-VkImageCopy-aspectMask-00142` while preserving the game's native stencil buffer untouched for deferred lighting.

### C. Temporal-First Under-Sampling
RAGE 8 was engineered around the PS4 Pro's hardware 2× MSAA checkerboard pipeline and temporal accumulation:
1. **Dithered Alpha Stippling**: Hair cards, beards, horse tails, and alpha foliage are rendered using single-bit alpha tests with an $8\times 8$ blue-noise dither matrix that shifts subpixel phases every frame.
2. **Volumetric Fog & Light Shafts**: Raymarched at quarter resolution ($1/4$ screen area) and temporally accumulated over multiple frames.
3. **Screen-Space Ambient Occlusion (SSAO)**: Sampled sparsely and accumulated over time.
- **CBR Mod Implementation**: Because RAGE 8 is a temporal-first engine, feeding quarter-resolution geometry passes into the mod's compute shader (`cbr_reconstruct.comp`) with YCoCg variance box clipping and history weighting naturally reconstructs these dithered effects into smooth, anti-aliased geometry identically to the PS4 Pro hardware pipeline.

---

## 3. Anatomy of a Single RAGE 8 (RDR2) Frame on Vulkan

When `RDR2.exe` records a frame on Vulkan, it executes the following stages in strict order:

| Stage # | RAGE 8 Frame Stage | Resolution & Format | Draw Calls | How `rdr2-cbr.asi` Interacts With It |
|---|---|---|---|---|
| **1** | **Resource Streaming & Mud/Snow POM Map** | $2048\times 2048$ R16_UNORM | ~10–50 | **Ignored by CBR**: Viewport dimensions do not match full scene (`w != dims.fullWidth`) and draw count is low (`< 250`). Accumulates dynamic terrain deformation (footprints, wagon ruts). |
| **2** | **Dynamic Environment Cubemap (6 Faces)** | $128\times 128$ per face (`RGBA8_SRGB`, `RGBA8_UNORM`, `D32S8` $\to$ `R11G11B10_FLOAT`) | ~30 / face | **Ignored by CBR**: Extent filter `extentMatchesMainScene == false` rejects 128×128 / 512×512 cubemap faces, preventing probe passes from corrupting temporal history. |
| **3** | **Terrain Depth Pre-Pass & G-Buffer Pass** | Full Target Res (`D32S8` + 5 MRTs: `RGBA8` / `R16G16B16A16_SFLOAT`) | 500 – 3,000+ | **Primary Interception Candidate**: Early depth prepass runs full res; main PBR geometry pass is downscaled to quarter resolution ($960\times 540$ or $640\times 360$) via `Hooked_vkCmdSetViewport`. |
| **4** | **Shadow Map Array Generation** | 2D Texture Arrays & Cube Arrays (`D16_UNORM` / `D32_SFLOAT`) | 200 – 1,500 | **Filtered out by CBR**: `RecordTrackedRenderPass` verifies `hasHdrColor == true`; depth-only passes are discarded so shadow cascades never trigger reconstruction. |
| **5** | **3× Depth Downsample Passes (SSAO, SSR, Volumetric Fog)** | Half / Quarter Res (`R32_SFLOAT`) | 1 – 5 each | **Filtered out by CBR**: Draw call gate (`draws >= 250u`) rejects downsample utility passes. Passes read pristine full-res depth from Stage 3. |
| **6** | **Deferred Lighting Stage (Sun/Moon + Stencil Local Lights)** | Target Res (`R16G16B16A16_SFLOAT` / `B10G11R11_UFLOAT`) | 10 – 150 | **Downscaled & Shaded**: Viewport remains downscaled; uses hardware-filtered `BlitToImage` for format 122 (`B10G11R11`) intermediate conversions on APUs. |
| **7** | **Forward Shading Stage (Translucent Glass, Water, Particles)** | Target Res (Main HDR Color + `D32S8` Depth) | 50 – 400 | **Reconstruction Anchor**: Translucent geometry renders back-to-front. `OnScenePassEnd` triggers `cbr_reconstruct.spv` immediately upon pass completion, right before PostFX. |
| **8** | **Post-Processing, 7-Mip Bloom & UI/HUD** | Full Target Res (`R11G11B10_FLOAT` $\to$ Swapchain Backbuffer) | 20 – 200 | **Runs at Native Resolution**: Tonemapping, bloom, and UI overlay execute at unscaled display resolution ($1280\times 720$, $1920\times 1080$, or $3840\times 2160$), keeping text and HUD razor-sharp. |

---

## 4. Architectural Evolution into RAGE 9 (GTA VI & GTA V Enhanced)

Rockstar used the 2025 PC update of Grand Theft Auto V Enhanced as a production testbed for architectural rendering subsystems developed for **RAGE 9** (Grand Theft Auto VI):

1. **Hardware Ray Tracing (RTGI, RTAO, and RT Reflections)**:
   - RAGE 7 and 8 relied on 6-face dynamic camera cubemaps and Screen-Space Reflections (SSR), which could not reflect dynamic vehicles behind the camera or handle complex indoor-outdoor multi-bounce lighting.
   - RAGE 9 transitions to BVH (Bounding Volume Hierarchy) hardware acceleration structures with real-time Ray-Traced Global Illumination (RTGI), dynamic colored bounce lighting, and real-time physical reflections.
2. **GPU-Driven Geometry Pipeline (Meshlets & Indirect Submission)**:
   - In RAGE 8 (RDR2), geometry culling and LOD selection are still primarily CPU-driven (`vkCmdDrawIndexed` loops generating thousands of draw calls, which is why the `drawCount >= 250u` heuristic is so reliable).
   - In RAGE 9, geometry submission shifts to GPU-driven pipelines using mesh shaders (`vkCmdDrawMeshTasksEXT`) and `ExecuteIndirect` with compute-driven frustum and occlusion culling.
3. **Physically Simulated 3D Water Grids & Strand-Based Hair**:
   - RDR2's planar water displacement maps and dithered alpha-card hair are replaced with full 3D FFT/Navier-Stokes fluid surface simulations and per-strand hair/fur physics.
4. **Native Decoupled Temporal Upscaling**:
   - Rather than relying on a fixed 2× MSAA checkerboard hardware pass like the PS4 Pro build of RDR2, RAGE 9 natively decouples internal rendering resolution from target display resolution to feed modern temporal upscalers (PSSR, FSR, DLSS) directly out of the forward shading stage with subpixel motion vectors.

---

## 5. Advanced Reconstruction Upgrades (Decima & DLSS 2.0 Parity)

Based on architectural breakdowns comparing **Death Stranding (Decima Engine / PS4 Pro CBR)** with **NVIDIA DLSS 2.0**, `rdr2-cbr` implements four targeted algorithmic upgrades directly in its compute reconstruction shaders (`cbr_reconstruct.comp`, `cbr_reconstruct_quad.comp`, and `cbr_reconstruct.hlsl`):

### Upgrade A: Specular & Highlight Anti-Flicker (Karis Inverse-Luma Weighting)
- **The Issue in PS4 Pro CBR**: When high-intensity specular highlights (e.g. lantern reflections on wet river mud or Arthur's leather coat, luminance $L > 10.0$) sit next to dark surroundings, unweighted statistical accumulation expands the standard deviation $\sigma$, blowing up the variance bounding box $\mu \pm 1.25\sigma$. This allows stale bright history pixels to survive across alternating frames, creating an aggressive 30 Hz specular strobe.
- **The Solution**: Step 4 weights each neighbor sample by Brian Karis's inverse-luma factor $w_i = \frac{1}{1 + \max(0, Y_i)}$:
  ```glsl
  float luma = (pc.u_ColorSpace == 1u) ? max(0.0, dot(c, vec3(0.299, 0.587, 0.114))) : max(0.0, c.x);
  float hdrWeight = 1.0 / (1.0 + luma);
  m1 += c * hdrWeight;
  m2 += (c * c) * hdrWeight;
  n += hdrWeight;
  ```
  Super-bright specular fireflies are bounded from distorting neighborhood variance, ensuring completely stable specular highlights.

### Upgrade B: Decima Gradient Spatial Fallback (Eliminating Diagonal Stair-Stepping)
- **The Issue in PS4 Pro CBR**: Standard 4-neighbor cardinal cross sampling (Left, Right, Up, Down) is densest horizontally and vertically, but structurally sparsest along $45^\circ$ diagonals. When history disoccludes, thin diagonal geometry (Saint Denis telegraph wires, wagon wheel spokes, gun barrels) exhibits stair-step aliasing.
- **The Solution**: Step 5 calculates color gradients across opposite cardinal pairs to measure local edge orientation:
  ```glsl
  vec3 horizDiff = abs(cardinalColor[0] - cardinalColor[1]);
  vec3 vertDiff  = abs(cardinalColor[2] - cardinalColor[3]);
  float horizGrad = dot(horizDiff, vec3(0.299, 0.587, 0.114));
  float vertGrad  = dot(vertDiff,  vec3(0.299, 0.587, 0.114));
  float dirBias[4] = float[](
      1.0 / (1.0 + horizGrad * 8.0), // Left
      1.0 / (1.0 + horizGrad * 8.0), // Right
      1.0 / (1.0 + vertGrad  * 8.0), // Up
      1.0 / (1.0 + vertGrad  * 8.0)  // Down
  );
  ```
  Interpolation weights preferentially follow the contour with lower gradient (along the edge) rather than blurring across it, preserving thin diagonal wires cleanly.

### Upgrade C: Dual-Metric Hair, Beard & Moss Disocclusion Fix
- **The Issue in PS4 Pro CBR**: RDR2 renders hair cards, beards, horse manes, and moss using alpha-to-coverage / stippled dither masks where adjacent pixels intentionally alternate between foreground hair depth ($0.5\,\text{m}$) and background sky/skin depth ($50.0\,\text{m}$). On reconstructed pixels, averaging cardinal depths produces a false intermediate depth ($25.25\,\text{m}$), causing Step 3 to falsely trigger disocclusion (`depthDelta > dynamicTolerance`), discarding temporal history and leaving a raw 1-pixel checkerboard pattern on Arthur's beard and hair.
- **The Solution**: In Step 1, inactive pixels track the closest foreground cardinal linear depth (`closestCardinalLin = min(closestCardinalLin, lin)`). In Step 3, the depth disocclusion delta tests both average and closest depth:
  ```glsl
  float deltaAvg     = abs(currentLinear - prevLin) / max(currentLinear, 1e-5);
  float deltaClosest = abs(closestCardinalLin - prevLin) / max(closestCardinalLin, 1e-5);
  depthDelta = isCurrentSampleActive ? deltaAvg : min(deltaAvg, deltaClosest);
  ```
  Stippled hair and foliage matching either average depth or individual foreground hair strands remain temporally accumulated, eliminating checkerboard holes on hair and beards.

### Upgrade D: Fused 5-Tap RCAS Contrast-Adaptive Sharpening
- **The Issue in PS4 Pro CBR**: Decima and native PS4 Pro CBR suffered from soft textures and blurred small text/gear decals due to Nyquist roll-off during corner averaging.
- **The Solution**: In Step 6, when `SharpeningStrength > 0.0`, a fused 5-tap RCAS (Robust Contrast-Adaptive Sharpening) block operates directly on the already-loaded `cardinalColor[0..3]` registers without additional texture bandwidth. Combined with the mod's negative MIP LOD bias (`MipLodBias = -1.0`), small text, compass markings, saddle leather engravings, and clothing weaves remain razor-sharp.

