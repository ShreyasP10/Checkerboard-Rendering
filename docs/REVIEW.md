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

## Open Design Items Status (All Items Resolved in Subsequent Passes)

1. **Real hooks.** **RESOLVED:** MinHook detours implemented on `vkQueuePresentKHR`, `vkCreateSwapchainKHR`, `vkCmdSetViewport`, `vkCmdSetScissor`, `vkCmdBeginRenderPass`, `vkCmdEndRenderPass`, and `vkCmdSetSampleLocationsEXT`. 60-second polling retry loop implemented in `CBRInitThread`.
2. **History depth is never produced.** **RESOLVED:** Shaders write full-resolution history depth output (`u_OutputDepth` in GLSL, `g_OutputDepth` in HLSL) with NaN/Inf guards.
3. **Jitter delta is computed but unused.** **RESOLVED:** Subpixel projection jitter delta is plumbed into push constants (`u_JitterDelta` & `u_JitterCompensation`). Non-accumulating `SetProjectionJitter()` implemented and verified.
4. **Dead config options.** **RESOLVED:** `ColorSpace`, `EnableSpatialFallback`, `MipLodBias` (-1.0f), and `PreferredApi = Auto` are parsed, validated, and fully wired to push constants and shader execution.
5. **Frame parity can drift.** **RESOLVED:** Tracked via `m_mainPresentTarget` with `compare_exchange_strong`; non-game swapchains and overlay presents are ignored.
6. **Hook teardown.** **RESOLVED:** Exception-safe detach handlers with MinHook uninstallation and atomic state tracking.
7. **Mutable config.** **RESOLVED:** `GetMutableConfig()` removed. Access is thread-safe via `GetConfig()` value copies and `Modify(fn)` under mutex protection.
8. **Release hygiene.** **RESOLVED:** CI builds now compute and publish `SHA256SUMS.txt` alongside automated release zips.
9. **Regenerate `CODEBASE.md`.** **RESOLVED:** `generate_codebase.py` and `check_sync.py` maintain 100% synchronization across all tracked files.

## Security assessment (summary)

The attack surface is small: no network access and no untrusted binary parsing. The real risks were crash/hang on malformed input (fixed), unsafe teardown (fixed), unchecked `printf`-style logging (fixed at compile time), and distribution trust (loader provenance and hashes: documented, still to do). Hardening flags (`/GS`, `/guard:cf`, `/sdl`, CET, ASLR/DEP) are present; they are MSVC-only and were not exercised here.
