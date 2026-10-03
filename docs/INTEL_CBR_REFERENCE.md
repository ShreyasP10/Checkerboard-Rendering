# Reference Analysis: Intel Checkerboard Rendering (CBR) White Paper

**Reference:** *Checkerboard Rendering for Real-Time Upscaling on Intel® Integrated Graphics*  
**Authors:** Trapper Mcferron & Adam Lake (Intel Corporation, August 9, 2018)  
**Sample Repository:** [github.com/GameTechDev/DynamicCheckerboardRendering](https://github.com/GameTechDev/DynamicCheckerboardRendering)  
**Document ID:** REF-INTEL-CBR-2018  
**Applicability to RDR2 CBR Mod:** Architectural blueprint for 2× MSAA sample quadrant mapping, Shade Resolve Targets (SRT) in deferred pipelines, linear depth disocclusion testing (CSO vs ASO), and jitter-cancellation fallbacks.

---

## 1. Executive Summary & Relevance

The Intel white paper provides the first publicly documented PC implementation of the hardware-assisted 2× MSAA Checkerboard Rendering technique originally popularized by the Sony PlayStation 4 Pro (Mark Cerny, 2016), Ubisoft Montreal (*Rainbow Six Siege*, Jalal El Mansouri, 2016), Guerrilla Games (*Decima Engine*, Giliam de Carpentier, 2017), and EA DICE (*Frostbite Engine*, Graham Wihlidal, 2017).

While Intel evaluated the technique on integrated GPUs to upscale from 540p to 1080p, the mathematical foundations, sample geometry, and shader algorithms apply directly to our PC mod reconstructing **1080p quarter-resolution 2× MSAA to 4K (3840×2160)** on NVIDIA Pascal GP104 hardware (GTX 1070 Ti) and modern discrete GPUs.

---

## 2. Core Concepts & Mathematical Formulations

### 2.1 Standard 2× MSAA Sample Positions & Quadrant Alignment

A standard 2× MSAA render target defines two sample points per pixel situated diagonally opposite within the pixel grid:
* In standard D3D12 and Vulkan rasterizer grids:
  $$\mathbf{S}_0 = \left(-\frac{4}{16}, -\frac{4}{16}\right) \quad (\text{Quadrant 2: Top-Left})$$
  $$\mathbf{S}_1 = \left(+\frac{4}{16}, +\frac{4}{16}\right) \quad (\text{Quadrant 4: Bottom-Right})$$

When rendering at quarter-resolution ($W/2, H/2$), each quarter-resolution pixel corresponds to a $2 \times 2$ block of presentation pixels:
* Sample $\mathbf{S}_0$ covers the top-left presentation pixel: Quadrant $(0, 0)$.
* Sample $\mathbf{S}_1$ covers the bottom-right presentation pixel: Quadrant $(1, 1)$.
* Quadrants $(1, 0)$ (top-right) and $(0, 1)$ (bottom-left) remain unshaded in Frame $N-1$.

### 2.2 Complementary Viewport Jitter Geometry

To cover the missing quadrants in the subsequent frame, the viewport in Frame $N$ is shifted **one full-resolution pixel to the right**:
$$\Delta X_{\text{full}} = +1.0 \implies \Delta X_{\text{quarter}} = +0.5 \text{ texels} = +\frac{8}{16}$$

Applying this horizontal shift to the fixed 2× MSAA sample pattern:
* Sample $\mathbf{S}_0$ moves to $\left(-\frac{4}{16} + \frac{8}{16}, -\frac{4}{16}\right) = \left(+\frac{4}{16}, -\frac{4}{16}\right)$ $\implies$ **Quadrant $(1, 0)$ (Top-Right)**.
* Sample $\mathbf{S}_1$ moves to $\left(+\frac{4}{16} + \frac{8}{16}, +\frac{4}{16}\right) = \left(+\frac{12}{16}, +\frac{4}{16}\right)$ $\implies$ wraps into **Quadrant $(0, 1)$ (Bottom-Left)** of the adjacent $2 \times 2$ cell.

```
Frame N-1 (Unjittered):
+-------------------+-------------------+
|  [ S0: Shaded ]   |    [ Empty ]      |  Quadrant (0,0) = S0
+-------------------+-------------------+
|    [ Empty ]      |  [ S1: Shaded ]   |  Quadrant (1,1) = S1
+-------------------+-------------------+

Frame N (Horizontal Jitter = +1 Full-Res Pixel / +0.5 Quarter Texel):
+-------------------+-------------------+
|    [ Empty ]      |  [ S0: Shaded ]   |  Quadrant (1,0) = S0
+-------------------+-------------------+
|  [ S1: Shaded ]   |    [ Empty ]      |  Quadrant (0,1) = S1
+-------------------+-------------------+

Combined (Full Reconstruction):
+-------------------+-------------------+
|  Frame N-1 (S0)   |   Frame N (S0)    |  100% Geometric Coverage
+-------------------+-------------------+
|   Frame N (S1)    |  Frame N-1 (S1)   |  Across All 4 Quadrants
+-------------------+-------------------+
```

This mathematical proof confirms that **a simple 1D horizontal subpixel jitter of $+0.5$ quarter-texels ($\Delta x = 0.5/W_{\text{render}}, \Delta y = 0$) achieves complete 4-quadrant geometric coverage across two consecutive frames**.

---

### 2.3 Deferred Shading Architecture: The Shade Resolve Target (SRT)

In deferred engines such as RDR2's RAGE engine, lighting and materials are decoupled:
1. **Phase 1 (G-Buffer):** Albedo, Normal, Material, Depth rendered at quarter-resolution ($W/2, H/2$) with 2× MSAA.
2. **Phase 2 (G-Buffer Resolve / Shade Resolve Target - SRT):**
   * Standard hardware resolves blend samples together, losing the per-sample checkerboard information.
   * Intel introduces the **Shade Resolve Target (SRT)**: a non-MSAA 2D texture created at **the same height as the quarter-resolution target, but twice the width**:
     $$\text{Width}_{\text{SRT}} = 2 \times \frac{W_{\text{target}}}{2} = W_{\text{target}}, \quad \text{Height}_{\text{SRT}} = \frac{H_{\text{target}}}{2}$$
   * The resolve shader computes lighting per MSAA sample and writes each sample into a unique texel:
     $$\text{Texel}(2x + 0, y) = \text{Shade}(\mathbf{S}_0)$$
     $$\text{Texel}(2x + 1, y) = \text{Shade}(\mathbf{S}_1)$$
3. **Phase 3 (Forward Transparency - CFB):** Transparent geometry rendered to a dedicated 2× MSAA Checkerboard Forward Buffer.
4. **Phase 4 (CBR Reconstruction):** Reconstruction compute shader samples SRT and CFB from frames $N$ and $N-1$ to output full 4K.

---

### 2.4 Handling Motion & Disocclusion: CSO vs. ASO

When the camera or scene objects move, motion vectors project the lookup in Frame $N-1$:
$$\mathbf{UV}_{\text{prev}} = \mathbf{UV}_{\text{curr}} - \mathbf{V}_{\text{pixel}}$$

#### Jitter Cancellation Condition
If camera translation exactly counteracts the subpixel jitter offset:
$$\text{quadrant}_{\text{needed}} \in \text{frame\_quadrants}_N$$
The required sample in Frame $N-1$ falls onto the same quadrant rendered in Frame $N$. The temporal sample is unavailable, and the shader flags `missing_shading = true` to trigger spatial fallback.

#### Occlusion Detection Strategies:
1. **Check Shading Occlusion (CSO):**
   * Reads 4 cardinal depth samples from Frame $N$ (Left, Right, Down, Up).
   * Converts all depth samples to **linear depth** ($Z_{\text{linear}}$).
   * Calculates the average linear depth of the 4 cardinal neighbors:
     $$Z_{\text{curr, avg}} = \frac{1}{4} \sum_{k=1}^4 Z_{\text{linear}}(k)$$
   * Compares against the linear depth fetched from Frame $N-1$:
     $$\Delta Z = |Z_{\text{prev, linear}} - Z_{\text{curr, avg}}|$$
   * If $\Delta Z \ge \text{Tolerance}$, the sample is marked occluded (`missing_shading = true`).
2. **Assume Shading Occluded (ASO):**
   * If pixel velocity exceeds a quarter-resolution texel ($|\mathbf{V}_x| > 0$ or $|\mathbf{V}_y| > 0$), assume occlusion immediately without reading neighbor depths.
   * **Performance:** Saves 5 depth texture fetches per pixel. Ideal for bandwidth-constrained Pascal GP104 / GTX 1070 Ti hardware in heavy scenes.

#### Cardinal Spatial Extrapolation
When `missing_shading == true`, the missing pixel color is extrapolated from Frame $N$'s 4 cardinal active samples:
$$C_{\text{reconstructed}} = \frac{\sum_{k=1}^4 w_k C_k}{\sum_{k=1}^4 w_k}$$

---

### 2.5 Linear Depth Conversion Formulations

Non-linear device depth ($Z_{\text{buffer}} \in [0, 1]$) allocates the vast majority of precision near the near clipping plane. Comparing raw buffer depth values produces severe distance-dependent bias:
* Over-rejection at near distances (causing excessive spatial blur).
* Under-rejection at far distances (causing temporal ghosting).

Linear depth conversion normalizes disocclusion thresholds across the entire frustum:

#### Standard Depth Buffer (Near = 0.0, Far = 1.0):
$$Z_{\text{linear}} = \frac{z_{\text{near}} \cdot z_{\text{far}}}{z_{\text{far}} - Z_{\text{buffer}} \cdot (z_{\text{far}} - z_{\text{near}})}$$

#### Reversed-Z Floating Point Buffer (Near = 1.0, Far = 0.0) [Used by RDR2 RAGE Engine]:
$$Z_{\text{linear}} = \frac{z_{\text{near}} \cdot z_{\text{far}}}{z_{\text{near}} + Z_{\text{buffer}} \cdot (z_{\text{far}} - z_{\text{near}})} \approx \frac{z_{\text{near}}}{Z_{\text{buffer}}}$$

Using $Z_{\text{linear}}$ guarantees that a relative threshold:
$$\frac{|\Delta Z_{\text{linear}}|}{Z_{\text{linear}}} > \text{DepthTolerance}$$
is mathematically scale-invariant from 0.1 meters to 10,000 meters.

---

## 3. Texture Sharpening via Mip LOD Bias

The Intel white paper confirms:
> *"In order to get increased texture resolution, a MIP LOD bias needs to be applied to textures... In Direct3D 12, use a D3D12_SAMPLER_DESC MipLODBias of -0.5f during the 3D scene pass."*

Because the geometry is rasterized at half shading density, standard anisotropic texture filtering selects mip levels that are 1 level coarser. Setting `MipLODBias = -0.5` shifts sampler lookups 0.5 levels finer, preserving full 4K Nyquist texture detail on quarter-resolution checkerboard geometry.

---

## 4. Key Takeaways Integrated into RDR2 CBR Mod

| Concept from Intel Paper | Status in RDR2 CBR Mod | Implementation Location |
|---|---|---|
| **2× MSAA 4-Quadrant Geometry** | Mathematically derived & applied | `include/cbr/jitter_manager.h`, `shaders/` |
| **Horizontal 1-Pixel Jitter (+0.5 texel)** | Standard complementary 2-phase jitter | `src/jitter_manager.cpp` |
| **Linear Depth Conversion** | Integrated into disocclusion & spatial fallback | `shaders/cbr_reconstruct.{comp,hlsl}` |
| **ASO vs CSO Dual-Mode Occlusion** | Selectable via `EnableMotionDilation` & config | `include/cbr/config.h`, `shaders/` |
| **Cardinal 4-Tap Spatial Fallback** | Cross-bilateral weighting with relative depth | `shaders/cbr_reconstruct.{comp,hlsl}` |
| **Texture Mip LOD Bias (-0.5f)** | Configured in `cbr.ini` & sampler hook design | `cbr.ini`, `include/cbr/config.h` |
