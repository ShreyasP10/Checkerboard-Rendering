# RDR2 Checkerboard Rendering (CBR)
## Final Architectural Specification & Review Report

**Date:** October 2026
**Project Lead:** Shreyas Pawar
**Target Environment:** Red Dead Redemption 2 (Vulkan / DX12 ASI Mod), targeting 4K reconstruction on Pascal architectures (e.g., GTX 1070 Ti).

---

## 1. Product Requirements Document (PRD)
**Objective:** Deliver a high-performance temporal checkerboard rendering upscaler (ASI plugin) to achieve 4K visual fidelity from a 1080p native render target, doubling framerates on older GPU hardware lacking tensor cores.
**Scope:** A local ASI/DLL single-player mod. No networked components, SQL, or HTTP traffic. Focus is entirely on local memory injection and GPU compute scheduling.
**Target Audience:** Users of legacy hardware wanting modern sub-pixel upscaling tech without purchasing DLSS/FSR hardware.

## 2. System Requirements Specification (SRS)
* **OS:** Windows 10/11 64-bit.
* **Graphics API:** Vulkan 1.3 or DirectX 12 Feature Level 12_0.
* **Game Engine:** RAGE Engine (RDR2 specific offsets).
* **Dependencies:** `dinput8.dll` (ASI Loader), MinHook (planned).
* **Shader Languages:** GLSL (Compute), HLSL (Compute Shader Model 6.0).

## 3. Technical Requirements Document (TRD) & Architecture
The plugin intercepts the presentation layer of the RAGE Engine to insert a custom GPU compute pass.
* **ConfigManager:** Thread-safe singleton parsing `cbr.ini`.
* **JitterManager:** Calculates and injects sub-pixel offsets (+0.5, -0.5) to the projection matrix on alternating frames.
* **RenderTargetManager:** Manages Vulkan/DX12 VRAM allocations (~315 MB for 4K CBR).
* **Reconstruction Pass:** 
  - **Step 1:** Native depth bounds sampling.
  - **Step 2:** Motion Vector Dilation (Velocity sampling).
  - **Step 3:** Temporal reprojection and History Depth Occlusion.
  - **Step 4:** Neighborhood Variance Clipping (YCoCg color space clamping) to eliminate temporal ghosting.
  - **Step 5:** Final spatial fallback and resolve.

## 4. Software Requirements Document (SRD) - Security & Robustness
The latest audit (v0.1.0-alpha) identified and resolved key operational risks for ASI mods:
* **Thread Safety:** Lock-free reads and exclusive write locks across render targets and configuration singletons prevent frame-tearing and data races during rendering.
* **NaN Poisoning:** Reconstructed pixels are mathematically guarded against `NaN`/`Inf` injection. A single NaN in the velocity vector or depth buffer is gracefully discarded, preventing the history ping-pong buffer from permanently black-screening.
* **Loader-Lock Safety:** The `DllMain` entry point delegates all initialization to lazy hooks, strictly adhering to Windows API best practices and preventing startup deadlocks.

## 5. Development Plan (DEV_PLAN) & Changelog
* **Phase 1 (Complete):** Core architecture, config systems, memory allocation logic, and robust temporal reconstruction shaders (GLSL & HLSL).
* **Phase 2 (Complete):** Security, reliability, race-condition hardening, and GitHub CI/CD automation (v0.1.0-alpha).
* **Phase 3 (Upcoming):** Hooking into the RAGE engine's internal D3D12/Vulkan swapchain structures via MinHook to extract native projection matrices and inject the jitter payload.

## 6. Risk Register
| Risk | Impact | Mitigation |
| :--- | :--- | :--- |
| **Data Races on Config** | High (Crash) | Replaced exposed pointers with lambda-based `Modify()` locks. |
| **NaN Shader Poisoning** | High (Visual) | Implemented strict `isinf`/`isnan` bounds in GLSL/HLSL and C++ host. |
| **DllMain Deadlock** | Critical (Crash) | Deferred `CreateThread` and hooks to lazy initialization. |
| **Config Injection (cbr.ini)** | Low (OOM/PII) | Strict bounds check (64KB limit, line count limit) and sanitization applied. |
| **Supply Chain Hijacking** | High (Malware) | Pinned GitHub actions, minimized `contents:write` scopes. |

---
*Report generated and validated for v0.1.0-alpha codebase.*
