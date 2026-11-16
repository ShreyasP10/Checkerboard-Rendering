# Architectural Design & Engineering Standards: Red Dead Redemption 2 CBR Engine

This document formalizes the core architectural principles, design patterns, and defensive engineering standards implemented across the C++20 Red Dead Redemption 2 Checkerboard Rendering (CBR) engine.

---

## 1. Architectural Principles: Modularity, Cohesion, and Coupling

The engine is partitioned to ensure clean separation of concerns, high intra-module focus, and minimal inter-module dependencies.

* **Modularity:** The system is divided into independent, separately compilable domain boundaries. Headers (`include/cbr/`) define public interfaces, implementations (`src/`) house logic, and private internal helpers are encapsulated within anonymous namespace `namespace {}` blocks to prevent symbol leaking across translation units.
  * *Codebase Mapping:* Strict separation across `config.cpp`, `logger.cpp`, `jitter_manager.cpp`, `pattern_scanner.cpp`, `render_target_manager.cpp`, and `reconstruction_pass.cpp`.
* **High Cohesion (Single Responsibility Principle):** All data and methods within a module focus on one explicit responsibility. State is grouped directly with the routines that mutate it, cleanly separating file I/O from GPU memory allocation.
  * *Codebase Mapping:* `checkerboard_mapping.h` computes $2\times$ MSAA sample coordinates (`MapPixelToSample`) with zero knowledge of Vulkan, DirectX 12, or file I/O.
* **Low Coupling:** Modules communicate through narrow, immutable data transfer objects (DTOs) rather than raw pointers or shared mutable state. A central mediator (`CBREngine`) coordinates execution without allowing subsystems to call each other arbitrarily.

---

## 2. Core Engineering Pillars

### Pillar 1: Maintainability, Reusability & Testing
* **Encapsulation & Blast Radius Reduction:** Internal state is hidden behind `private:` class members, exposed exclusively through thread-safe mutators and value-returning getters (e.g., `GetConfig()` returning value copies rather than raw references to eliminate unmanaged mutation risks).
* **Context-Independent Reusability:** Pure mathematical utilities and coordinate mappers rely solely on standard library headers (`<cstdint>`, `<cmath>`), allowing them to be extracted and dropped into other C++ graphics projects untouched.
* **Isolated Unit Testing:** Core math, pattern scanning, and configuration subsystems compile into standalone unit test suites (`cbr_tests`) without linking heavy OS graphics runtimes or graphics driver hooks.

### Pillar 2: Collaboration & Thread Safety
* **Contract-First Interfaces:** Public header contracts are defined prior to implementation, allowing parallel multi-developer workflows without Git merge conflicts.
* **Strict Lock Hierarchy:** Deadlocks are eliminated by enforcing an explicit multi-level lock acquisition order across modules:

$$\text{Level 1: ConfigManager::m\_mutex} \longrightarrow \text{Level 2: JitterManager::m\_mutex} \longrightarrow \text{Level 3: Logger::m\_mutex}$$

* **Extension Points:** Registration callbacks (e.g., `UIOverlay::RegisterHudRenderCallback`) allow UI and telemetry extensions to subscribe to metrics without touching core engine loops.

### Pillar 3: Adaptability & Strategy Patterns
* **Data-Driven Configuration:** Tunable thresholds, resolutions, and features are externalized into `cbr.ini`, allowing the exact same binary (`rdr2-cbr.asi`) to scale from a discrete NVIDIA GTX 1070 Ti at 4K down to an integrated AMD Radeon Vega 7 APU at 720p.
* **Hardware Strategy Switcher:** Sampling strategies (Strategy A viewport half-pixel shift vs. Strategy B hardware sample-locations extension) are isolated behind runtime capability checks.

### Pillar 4: Abstraction & Obsolescence Resilience
* **API Abstraction:** Mid-frame reconstruction (`CBREngine::OnScenePassEnd`) abstracts execution details, allowing the engine core to remain agnostic to whether a `VkCommandBuffer` or an `ID3D12GraphicsCommandList` is backing the dispatch.
* **Game Patch Resilience:** Pattern scanner AOB signatures (`48 8B 05 ? ? ? ? 48 8B 48 08`) include graceful fallbacks; if a Rockstar game update breaks a signature, the scanner returns false and hooks fall back safely to Strategy A rather than crashing.
* **Legacy Config Compatibility:** Deprecated ini keys (e.g., `JitterScale`) are retained during parsing to prevent breaking older user setups while emitting clear migration warnings.

---

## 3. Defensive Programming & Portability

The engine operates under a Zero-Trust security and validation model:

```cpp
// Example: Defensive depth linearization preventing GPU/CPU state corruption
inline float LinearizeDepth(float d, DepthConvention convention, float zNear, float zFar) {
    if (!std::isfinite(d) || zNear <= 0.0f) {
        return std::numeric_limits<float>::quiet_NaN(); // Fail-safe poison value
    }
    const float denom = (convention == DepthConvention::Reversed)
        ? (zNear + d * (zFar - zNear))
        : (zFar - d * (zFar - zNear));
    if (!std::isfinite(denom) || std::fabs(denom) < 1e-7f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return (zNear * zFar) / denom;
}
```

* **Bounded Input Parsing:** `ConfigManager` enforces strict file size limits (64 KiB), line limits (500 lines), NaN/Inf rejection, and integer clamping.
* **Compile-Time ABI Guards:** Static assertions (`static_assert(sizeof(ReconstructionPushConstants) == 80)`) guarantee struct layout safety between C++ host code and SPIR-V/DXIL shaders.
* **Memory Page Safety:** Memory scanning validates page states via `VirtualQuery` (`MEM_COMMIT`, avoiding guard pages) and wraps pointer dereferences in Structured Exception Handling (`__try` / `__except`).
* **Shader NaN/Inf Sanitization:** Compute shaders check `isnan()` and `isinf()` on depth, velocity, and color inputs prior to image storage, ensuring a single corrupted GPU texel never poisons temporal history buffers.
* **Cross-Platform Portability:** Standard C++20 facilities are used universally, with OS-specific hooks (`_WIN32`) cleanly isolated from portable fallback stubs.
