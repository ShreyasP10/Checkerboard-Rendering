# Architectural Design & Engineering Standards: Red Dead Redemption 2 CBR Engine

Building production-grade software requires translating high-level software engineering concepts—modularity, high cohesion, loose coupling, abstraction, portability, testability, and defensive design—into enforceable compile-time rules, strict interface boundaries, and automated verification gates.

In software engineering, "perfect" software does not mean code that never changes; it means code with zero unhandled fault states, high immunity to side effects, and the lowest possible cost of change.

---

## 1. Enforcing Modularity, High Cohesion & Loose Coupling

To prevent a codebase from decaying into a "Big Ball of Mud," enforce strict boundaries at the file, class, and function levels:

### A. Physical & Logical Modularity (Information Hiding)
* **Public Contract vs. Private Mechanism:** Expose only the minimum necessary types and method signatures in header files (`include/`), and hide all helper functions, OS structs, and internal state inside implementation files (`src/`) using anonymous namespaces (`namespace { ... }`) or `private:` members.
* **Single Source of Truth for Constants:** Centralize shared limits, buffer sizes, and mathematical constants into a single header (`include/cbr/limits.h`) so magic numbers never drift between modules.

### B. Functional Cohesion (The Single-Responsibility Rule)
A module has High (Functional) Cohesion when every function and variable inside it contributes to a single, well-defined responsibility:
* **Separate Policy from Mechanism:**
  * **Policy modules** decide *what* should happen and when (e.g., `CBREngine` manages frame lifecycle and parity; `ConfigManager` manages settings validation).
  * **Mechanism modules** execute *how* a specific task is performed on hardware or memory (e.g., `ReconstructionPass` dispatches compute shaders; `PatternScanner` scans memory bytes; `checkerboard_mapping.h` computes $2\times$ MSAA sample coordinates).
* **The Cohesion Litmus Test:** Describe what a class does in one sentence. If you have to use the word "and" (e.g., *"This class parses the config file AND allocates Vulkan textures"*), split the class into two modules.

### C. Loose Coupling (Data & Mediator Coupling Only)
Software modules can be coupled in six ways (from best to worst). Always restrict your architecture to the top two:

| Coupling Type | Quality | How to Implement / Enforce It | Example in Practice |
|---|---|---|---|
| **1. Data Coupling** | **Best** | Pass only primitive values or small immutable structs needed for the calculation. | `LinearizeDepth(float d, DepthConvention conv, float zNear, float zFar)` in `depth_convention.h`. |
| **2. Stamp (DTO) Coupling** | **Good** | Pass read-only value snapshots (Data Transfer Objects) across boundaries instead of live internal references. | `ConfigManager::Get().GetConfig()` returns a thread-safe value copy of `CBRConfig`; `BuildReconstructionPushConstants()` packs an 80-byte struct. |
| **3. Control Coupling** | **Acceptable (Minimize)** | Passing boolean flags that alter another module's internal control flow. | `Shutdown(bool isProcessExit)`: Keep control flags at top-level lifecycle boundaries only. |
| **4. Common Coupling** | **Avoid** | Multiple modules reading/writing shared global variables directly. | Encapsulate state inside thread-safe managers (`RenderTargetManager`, `JitterManager`) rather than raw globals. |
| **5. Content Coupling** | **Forbidden** | One module directly mutating another module's private members or holding an unlocked reference to internal state. | Removing `GetMutableConfig()` and replacing it with `ConfigManager::Modify(Fn&& fn)` so external callers can never bypass the mutex. |

#### Use the Mediator Pattern to Prevent Cross-Module Tangling
Subsystems (`JitterManager`, `RenderTargetManager`, `ReconstructionPass`, `UIOverlay`) do not call each other in a circular web. Instead, route lifecycle events through a central mediator (`CBREngine::OnBeginFrame`, `OnScenePassEnd`, `OnPrePresent`, `OnSwapchainRecreated`).

---

## 2. Implementing Abstraction, Flexibility, Obsolescence Protection & Reuse

```
+-------------------------------------------------------------------+
|                   High-Level Policy (CBREngine)                   |
|        OnBeginFrame()  |  OnScenePassEnd()  |  OnPostPresent()    |
+------------------------+--------------------+---------------------+
                         | (Abstract API Boundary)
         +---------------+---------------+
         |                               |
         v                               v
+------------------------+     +------------------------+
|   Vulkan Backend       |     |   DirectX 12 Backend   |
|  - DispatchVulkan()    |     |  - DispatchDX12()      |
|  - VkCommandBuffer     |     |  - ID3D12CommandList   |
+------------------------+     +------------------------+
```

### A. Abstraction & Design for Flexibility (Open/Closed Principle)
* **Abstract the "What" from the "How":** High-level workflows call uniform lifecycle methods (`InitializeVulkan` / `InitializeDX12`, `DispatchVulkan` / `DispatchDX12`) while low-level descriptor binding and barrier synchronization remain encapsulated inside `ReconstructionPass`.
* **Data-Driven Extension:** When supporting new hardware profiles or algorithms, drive them through configuration data (`cbr.ini`) and shader uniforms/push constants (`CBRCameraUniforms`, `ReconstructionPushConstants`) rather than hardcoding branches across the engine.

### B. Anticipate Obsolescence (Anti-Corruption Layers & Graceful Fallbacks)
External dependencies—game binaries, OS headers, compiler toolchains, and GPU drivers—eventually change or become unavailable. Protect your software with three techniques:
* **Runtime Dynamic Symbol Resolution:** Instead of hard-linking to a specific SDK version that might not exist on a user's machine or build server, resolve API functions dynamically at runtime (`vkGetDeviceProcAddr`, `GetProcAddress`, `dlopen`) and provide headerless ABI structs guarded by `static_assert`.
* **Multi-Tiered Graceful Degradation:** Never rely on a single brittle mechanism when interacting with an external system. For example, if `PatternScanner` cannot locate the game's camera matrix after a game update, the engine logs a diagnostic message and automatically falls back to Strategy A (Viewport Shift Redirection) so the software continues functioning.
* **Backward-Compatible Config Deprecation:** When a setting becomes obsolete (such as `JitterScale` in `cbr.ini`), continue parsing it in `ConfigManager::Load`, emit a clear warning explaining why it is ignored, and preserve valid behavior rather than crashing on older configuration files.

### C. Systematic Code & Design Reuse
* **Write Context-Independent Utilities:** Design mathematical and algorithmic functions (`InvertMatrix4x4` in `reconstruction_pass.cpp`, `FindPattern` in `pattern_scanner.cpp`, `MapPixelToSample` in `checkerboard_mapping.h`) so they depend only on the C++ standard library. They can then be reused verbatim in other projects or verified in standalone Python/C++ test harnesses.
* **Reuse Verified Third-Party Libraries:** Use proven, single-purpose libraries for complex low-level tasks (such as `vendor/minhook` for x64 instruction trampolines) rather than maintaining custom disassemblers.

---

## 3. Implementing Portability & Defensive Design (Zero-Trust Engineering)

### A. Design for Portability
* **Write 90% of Logic in Strict Standard C++20:** Keep `CMAKE_CXX_EXTENSIONS OFF` in `CMakeLists.txt` so compiler-specific extensions never creep into core modules. Use `<filesystem>`, `<atomic>`, `<mutex>`, `<shared_mutex>`, and `<charconv>` (which is locale-independent, preventing bugs on European systems where `,` is the decimal separator).
* **Isolate Platform & ISA Specifics Behind Leaf Wrappers:**
  * **OS Isolation:** Confine `<windows.h>` and `<dlfcn.h>` to thin helper functions (`GetVulkanLoaderModule()`, `GetVulkanLoaderSymbol()`, `CrashHandler`) with `#if defined(_WIN32)` / `#else` branches so the core engine compiles cleanly on both Windows (MSVC/MinGW) and Linux (GCC/Clang).
  * **CPU SIMD Isolation:** Check hardware capabilities at runtime (`DetectAVX2()` in `pattern_scanner.cpp`) and provide a tiered fallback chain: $\text{FindPatternAVX2} \longrightarrow \text{FindPatternSSE2} \longrightarrow \text{portable std::memchr}$.
  * **Cross-Platform Types:** Avoid `unsigned long` when parsing 64-bit values (`unsigned long` is 32-bit on Windows LLP64 but 64-bit on Linux LP64); explicitly use `uint32_t`, `uint64_t`, `size_t`, and `std::stoull`.

### B. Design Defensively (The 6 Layers of Defense)
Defensive programming assumes that every external file, pointer, thread, and GPU buffer is potentially corrupted or hostile:

```cpp
// Layer 1: Compile-time type & ABI defense (include/cbr/logger.h & reconstruction_pass.h)
static_assert((!std::is_same_v<std::decay_t<Args>, std::string> && ...),
              "Pass std::string arguments as .c_str() to CBR_LOG_* macros");
static_assert(sizeof(ReconstructionPushConstants) == 80,
              "Push constant layout drifted from shaders");
```

1. **Compile-Time Invariants (`static_assert`):** Catch bugs before the binary is even linked by asserting struct sizes (`sizeof`), constant buffer 16-byte alignment (`sizeof % 16 == 0`), struct member byte offsets (`offsetof`), and template parameter types.
2. **Strict Input Sanitization & Resource Caps:**
   * Check file size (`kMaxConfigFileBytes = 64 KiB`), line count (`kMaxConfigLines = 500`), and line length (`kMaxConfigLineChars = 1024`) before reading into memory to prevent out-of-memory (OOM) denial of service.
   * Reject trailing garbage on numbers (`"4k"` or `"1xyz"`), clamp values to safe domain bounds (`[kMinTargetWidth, kMaxTargetWidth]`), and strip `\r`/`\n` from user strings before logging.
3. **Numerical & Shader NaN/Inf Firewalls:**
   * In C++ and GLSL/HLSL, NaN comparisons (`x > tolerance`) evaluate to false, allowing a single NaN texel or division-by-zero to permanently poison temporal feedback loops.
   * Guard divisions with `std::fabs(denom) < 1e-7f`, check `std::isfinite()` / `isnan()` / `isinf()` at every stage, and sanitize output colors before `imageStore` / UAV write.
4. **Memory Page & Pointer Validation:**
   * When reading process memory (`pattern_scanner.cpp`), query the OS virtual memory manager (`VirtualQuery`) region-by-region to verify `MEM_COMMIT`, readable page permissions, and absence of `PAGE_GUARD | PAGE_NOACCESS`, wrapped inside `__try / __except` (SEH).
   * When loading system DLLs (`dbghelp.dll`, `d3d12.dll`, `d3dcompiler_47.dll`), enforce `LOAD_LIBRARY_SEARCH_SYSTEM32` to prevent DLL preloading/hijacking.
5. **Concurrency & Lock-Ordering Discipline:**
   * Establish and document a global lock hierarchy:
     $$\text{Level 1: ConfigManager::m\_mutex} \longrightarrow \text{Level 2: JitterManager::m\_mutex} \longrightarrow \text{Level 3: Logger::m\_mutex}$$
   * Never acquire a higher-level lock while holding a lower-level lock.
   * Offload blocking disk I/O to a background worker thread with a bounded ring queue (`kMaxQueueMessages = 2048`) so the real-time render thread never stalls.
6. **Fail-Open Exception & Gate Boundaries:**
   * Wrap all hooked callbacks (`Hooked_vkQueuePresentKHR`, `Hooked_vkCmdEndRenderPass`, `Hooked_D3D12Present`) in `try { ... } catch (...) {}` so an internal C++ exception never propagates into the host application's stack.
   * If prerequisites for an operation are not met (such as `!copiedColor || !copiedDepth` in `Hooked_vkCmdEndRenderPass`), fail open: abort the pass cleanly and preserve the host application's native state rather than writing incomplete data.

---

## 4. Implementing Design for Testability & Collaboration

To make software easy to verify and safe for multiple engineers to collaborate on, four tiers of automated testing are built into `CMakeLists.txt` and the CI pipeline (`.github/workflows/build.yml`):

* **Tier 1 — Pure Host Unit Tests (`tests/test_core.cpp`):**
  Test configuration parsing, malformed `.ini` inputs, logger level filtering, matrix inversion, depth linearization, and geometric coverage simulations (`TestCheckerboardGeometry`) in milliseconds on any CPU without needing a GPU or Windows OS.
* **Tier 2 — Link-Time Dependency Injection / Fakes (`tests/test_engine.cpp`):**
  By keeping `hooks_vulkan.cpp` and `hooks_dx12.cpp` in separate translation units from `hooks.cpp` and `cbr_engine.cpp`, `cbr_engine_tests` can supply fake `InstallVulkanHooks()` / `InstallDX12Hooks()` implementations at link time to test hook retry loops, frame parity, and swapchain recreation deterministically.
* **Tier 3 — Cross-Language Single-Source Verification (`tests/check_shader_mapping.py`):**
  When an algorithm must exist in both C++ (`checkerboard_mapping.h`) and GPU shaders (`cbr_reconstruct.comp`, `cbr_reconstruct.hlsl`), run an automated Python equivalence test in CI that proves all implementations produce identical outputs across tens of thousands of coordinate combinations.
* **Tier 4 — Headless GPU Pipeline Execution (`tests/test_vulkan_headless.cpp`):**
  Execute the actual compiled SPIR-V compute shaders (`cbr_reconstruct.spv`) against a software Vulkan ICD (such as Mesa Lavapipe in Linux CI) to catch descriptor binding mismatches, barrier errors, and shader bugs automatically on every pull request.

---

## 5. Pre-Merge Architectural Verification Checklist

Use this 7-point checklist before merging any new feature or module into the project:

| Principle | Verification Question | Pass Criterion |
|---|---|---|
| **Modularity** | Can this `.cpp` file be compiled and understood without reading the internals of other `.cpp` files? | Zero non-const global variables shared across files; internal helpers are inside `namespace {}`. |
| **Cohesion** | Does every function in this class operate on the same core state for a single purpose? | Class description requires no "and" conjunctions; policy is separated from low-level API calls. |
| **Coupling** | How does this module exchange data with other modules? | Uses value copies/DTOs (`GetConfig()`, `ReconstructionPushConstants`) and respects the lock hierarchy. |
| **Flexibility & Obsolescence** | What happens if an external dependency (game binary, GPU feature, config key) is missing or changes? | Degrades gracefully to a safe fallback (Strategy A, CPU timer fallback, default config) with a rate-limited log warning. |
| **Portability** | Does the code rely on OS-specific headers or compiler-specific sizes in core logic? | Core files compile with `-Wall -Wextra -Wpedantic` on GCC/Clang/MinGW and `/W4` on MSVC. |
| **Defensive Design** | What happens if an input is `nullptr`, `0`, negative, NaN, Inf, or oversized? | Guarded at the entry boundary; `static_assert` pins ABI layouts; no locks or heap allocations in crash handlers. |
| **Testability** | Can this feature be tested in automated CI without manual human intervention? | Covered by a deterministic test in `ctest` (`cbr_tests`, `cbr_engine_tests`, or `cbr_vulkan_headless`). |
