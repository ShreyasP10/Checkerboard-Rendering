# RDR2 CBR Mod: Code Review & Fix Report

Scope: `CODEBASE.md` + `readme.md` (second revision). `docs/` was not provided.

## How the fixes were verified

| Check | Result |
|---|---|
| Original `logger.cpp` compile | **Fails** (`ms` undeclared), confirmed with g++ |
| All sources, patched, `g++ -std=c++20 -Wall -Wextra` | Clean |
| All sources, patched, MinGW-w64 cross-compile (Windows APIs) | Clean (one benign `GetProcAddress` cast warning) |
| Full CMake configure + build + link of `rdr2-cbr.asi` (MinGW) | Success |
| Shaders via `glslangValidator -V` (both `.comp`) | Valid SPIR-V |
| Config/logger behavioral test (bad input, buffering, log level) | All asserts pass |
| `cbr-fixes.patch` applies cleanly to the uploaded originals | Yes |

**Not verified:** MSVC-specific flags (`/guard:cf`, `/GL`, `/CETCOMPAT`), the `dxc`/HLSL build (dxc unavailable here), and any in-game behavior. The project has no live hooks yet.

---

## Fixed in this pass

| # | Issue | Fix |
|---|---|---|
| 1 | **Build break:** `logger.cpp` used undeclared `ms` | Restored the `duration_cast` line |
| 2 | `DllMain` waited on a thread under the loader lock (deadlock up to 2s, then possible crash in unmapped code) | Module is pinned with `GetModuleHandleExW(PIN)`; `DLL_PROCESS_DETACH` does nothing unsafe |
| 3 | Early log lines (config not found / loaded) were lost because the logger started after the config load | Logger buffers up to 256 early messages, flushes on `Initialize`, discards on `Disable()` |
| 4 | `LogLevel` in the ini was parsed but ignored | Wired to `Logger::SetMinLevel` |
| 5 | Config accepted `4k` as 4 and `-1` as 4294967295 (then clamped to odd values) | Trailing text and negatives are rejected and the default is kept |
| 6 | `GetModuleFileNameA` + `MAX_PATH` broke on non-ASCII or long install paths | `GetModuleFileNameW` with a growing buffer; paths are `std::filesystem::path` end to end |
| 7 | Hooks reported "successfully registered" while installing nothing | `Install*Hooks()` now log a warning and return `false` until real detours exist |
| 8 | Hook trampolines with a null original silently returned success (frames or swapchains dropped) | Return an error code instead; exceptions can no longer escape into the render thread |
| 9 | A `VkQueue` / `IDXGISwapChain` was passed where a command buffer / list is expected | Engine passes `nullptr` with an explanatory comment |
| 10 | Data races on hook flags, pass-initialized flag, history ping-pong index | `std::atomic` |
| 11 | `LogFmt` could take a `std::string` through C varargs (UB) | `static_assert` rejects it at compile time; `<cstdio>` included |
| 12 | CMake: redundant defines, `LANGUAGES C`, duplicate `CBR_VULKAN_SUPPORT`, no static CRT, no shader build | Cleaned up; static CRT; optional `glslangValidator` / `dxc` custom commands |
| 13 | README overclaims (unprojection fallback, shared-memory tiling, ImGui overlay and F11 toggle, hook-overhead figure, fake "registered" log line, vague ASI loader source) | Corrected or marked `[ ]` |
| 14 | CONTRIBUTING said GLSL 4.60 but shaders use `#version 450` | Corrected |

## Still open (design work, intentionally not patched)

1. **Real hooks.** Integrate MinHook (or equivalent), detour `vkQueuePresentKHR` / `vkCreateSwapchainKHR` / `IDXGISwapChain::Present`, and set `g_Original_*`. Add a retry when `vulkan-1.dll` / `d3d12.dll` load later than the plugin.
2. **History depth is never produced.** The shaders sample `u_HistoryDepth`, but nothing writes it, so disocclusion is undefined until a history-depth output is added.
3. **Jitter delta is computed but unused.** `GetJitterDelta()` is never passed to the shader. The correct reprojection offset depends on the final jitter/projection convention, so it should be derived and tested against real RDR2 matrices rather than guessed. Also note `Apply/RemoveJitterFromProjection` use `+=`/`-=` and accumulate if called twice per frame.
4. **Dead config options.** `ColorSpace`, `EnableSpatialFallback`, `JitterScale`, `JitterPattern` (Halton) and `MipLodBias` are parsed but not consumed. `PreferredApi = Auto` cannot be parsed.
5. **Frame parity can drift.** `m_frameIndex` increments on every present, including non-game swapchains or loading screens.
6. **Hook teardown** has no wait for in-flight presents. Moot until real hooks exist; design it with them.
7. **Mutable config** (`GetMutableConfig`) is not synchronized. It will matter once the overlay edits values live.
8. **Release hygiene.** Publish SHA-256 hashes for builds; consider refusing to initialize when anti-cheat modules are present (the offline-only policy is currently unenforced).
9. **Regenerate `CODEBASE.md`.** It is now out of date relative to these changes.

## Security assessment (summary)

The attack surface is small: no network access and no untrusted binary parsing. The real risks were crash/hang on malformed input (fixed), unsafe teardown (fixed), unchecked `printf`-style logging (fixed at compile time), and distribution trust (loader provenance and hashes: documented, still to do). Hardening flags (`/GS`, `/guard:cf`, `/sdl`, CET, ASLR/DEP) are present; they are MSVC-only and were not exercised here.
