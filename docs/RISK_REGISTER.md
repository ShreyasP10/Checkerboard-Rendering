# Risk Register (RISK_REGISTER)

**Project Name:** RDR2 Checkerboard Rendering Mod (CBR)  
**Document ID:** RISK-RDR2CBR-001  
**Version:** 1.0  
**Author & Co-Owner:** Shreyas Pawar  
**Date:** September 2026  

---

## 1. Risk Assessment Methodology

Risks are assessed using a standard $5 \times 5$ Probability versus Impact matrix:

$$\text{Risk Score} = \text{Probability (1–5)} \times \text{Impact (1–5)}$$

- **Critical (Score 16–25):** Immediate blocker; requires primary architecture fallback.
- **High (Score 10–15):** Major obstacle; requires active mitigation strategy.
- **Medium (Score 5–9):** Manageable issue; monitor and mitigate.
- **Low (Score 1–4):** Minor operational friction; address during normal polish.

---

## 2. Risk Matrix & Summary Table

| Risk ID | Category | Description | Prob (1-5) | Imp (1-5) | Score | Severity |
|---|---|---|---|---|---|---|
| **RSK-01** | Technical | RAGE internal motion vectors are inaccessible or encrypted in memory. | 4 | 5 | **20** | **Critical** |
| **RSK-02** | Technical | RAGE render target allocation calls use custom heap allocators bypassing standard API hooks. | 3 | 4 | **12** | **High** |
| **RSK-03** | Technical | Pascal GPU architecture memory bandwidth overhead negates frame-time savings at 4K. | 3 | 4 | **12** | **High** |
| **RSK-04** | Technical | Hair and foliage dithering in RDR2 clashes with subpixel checkerboard patterns. | 4 | 3 | **12** | **High** |
| **RSK-05** | Platform | RDR2 title updates alter memory layout, invalidating hook patterns and offsets. | 4 | 3 | **12** | **High** |
| **RSK-06** | Legal/Policy | Accidental loading into Red Dead Online triggers BattlEye or Rockstar anti-cheat ban. | 2 | 5 | **10** | **High** |
| **RSK-07** | Quality | Temporal ghosting on rapid camera cuts or fast horse traversal. | 3 | 3 | **9** | **Medium** |
| **RSK-08** | Performance | Compute shader register spilling degrades occupancy on Pascal GP104. | 2 | 3 | **6** | **Medium** |

---

## 3. Detailed Risk Analysis & Mitigation Strategies

### RSK-01: Inaccessible Internal Motion Vectors
- **Root Cause:** RDR2 computes motion vectors internally for its native TAA pass, but does not expose an accessible render target name or uniform buffer binding.
- **Impact:** Without accurate per-pixel velocity, moving objects (horses, NPCs, trees) will exhibit severe ghosting, smearing, and trailing artifacts.
- **Mitigation Strategy:**
  1. *Primary Plan:* Hook the exact vertex/pixel shaders used by the engine's velocity pre-pass or G-Buffer pass, duplicating the output into a custom mod-allocated `RG16F` velocity texture.
  2. *Contingency Fallback:* Implement a Camera Reprojection Depth-Unproject Fallback pass. By tracking $\mathbf{M}_{\text{viewproj}}(t)$ and $\mathbf{M}_{\text{inv\_viewproj}}(t-1)$ alongside the depth buffer, static geometry velocity can be calculated with 100% mathematical accuracy. Dynamic objects are then detected via depth discontinuities and filtered with a spatial cross-bilateral blur.

---

### RSK-02: Bypassing Standard API Hook Calls
- **Root Cause:** RAGE engine frequently sub-allocates from large contiguous memory pools (`VkDeviceMemory` or ID3D12Heap) rather than calling `vkCreateImage` or `CreateCommittedResource` individually.
- **Impact:** Hooking standard creation functions will fail to detect individual render targets.
- **Mitigation Strategy:**
  - Hook at the render pass and descriptor set binding level (`vkCmdBeginRenderPass`, `vkCmdBindDescriptorSets`, `vkCmdSetViewport`).
  - Identify render targets by their bind slots, format descriptors, and viewport dimensions ($3840 \times 2160$) rather than initial allocation calls.

---

### RSK-03: Pascal Memory Bandwidth Saturation
- **Root Cause:** The GTX 1070 Ti features 256 GB/s memory bandwidth. Reading multiple full-resolution history buffers (Color, Depth, Motion) during the reconstruction pass could incur heavy memory bus latency.
- **Impact:** Frame-time savings from quarter-resolution rendering might be partially consumed by memory transfer overhead.
- **Mitigation Strategy:**
  - Pack history formats efficiently: Color history in `B10G11R11_UFLOAT` or `RGBA8_UNORM` instead of `RGBA16F` if precision allows.
  - Employ $18 \times 18$ threadgroup shared memory tiling to cache texture reads across warps, reducing global memory transactions by up to 72%.

---

### RSK-04: Alpha Foliage & Hair Dithering Conflicts
- **Root Cause:** RDR2 uses screen-door stippling/dithering for hair cards and leaf transparencies. Checkerboard sampling can alias with this dithering pattern, creating stippling moiré or checkerboard tooth artifacts.
- **Impact:** Visible grid artifacts on horse manes, tails, and forest canopies.
- **Mitigation Strategy:**
  - Apply a bilateral spatial resolve filter specifically to pixels marked with high depth variance or high-frequency luminance variance.
  - Apply the reconstruction pass *before* the game's native TAA pass so that TAA can smooth out remaining subpixel stipple patterns.

---

### RSK-05: Title Updates & Memory Pattern Invalidation
- **Root Cause:** Rockstar occasionally updates RDR2 game executables, changing function offsets and memory signatures.
- **Impact:** Mod fails to inject or crashes on game startup after an update.
- **Mitigation Strategy:**
  - Utilize robust wildcard byte pattern scanning (AOB scanning) rather than hardcoded static memory offsets.
  - Implement version verification on startup; if signatures fail to resolve, gracefully log the error to `cbr.log` and disable hooks without crashing the host process.

---

### RSK-06: Anti-Cheat & Red Dead Online Interference
- **Root Cause:** RDR2 includes Red Dead Online with anti-cheat monitoring. Injecting DLLs into online sessions can cause permanent account bans.
- **Impact:** User account sanctions from Rockstar Games.
- **Mitigation Strategy:**
  - Hardcode an active session state check: If network/multiplayer session flags are detected in game memory, the mod immediately self-terminates and unhooks all functions.
  - Display prominent warnings in the documentation and in-game ImGui overlay indicating the mod is strictly for Single Player / Story Mode.
