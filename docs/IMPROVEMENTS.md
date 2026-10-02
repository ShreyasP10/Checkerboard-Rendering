# RDR2 CBR Mod: Detailed Improvements & Roadmap

Companion to `REVIEW.md`. Part 1 lists what was implemented and verified in this pass. Parts 2 to 6 are prioritized suggestions with enough detail to act on. Code in Parts 2 to 6 is **illustrative sketches, not tested**.

---

## Part 1: Implemented and verified in this pass

| Area | Change | Verified by |
|---|---|---|
| Shaders | `cbr_reconstruct.{comp,hlsl}` now **write a full-res history-depth target** (`u_OutputDepth` / `g_OutputDepth`). Before, nothing produced the depth that the disocclusion test reads. | glslang (GLSL + HLSL front-end) |
| Shaders | History depth is read with an exact `texelFetch` (GLSL), so a linear sampler can no longer blend depth across edges | glslang |
| Shaders | `ColorSpace` (YCoCg/RGB) and `EnableSpatialFallback` now actually control the shader (push constants / cbuffer) | glslang |
| C++ | Push-constant struct is `static_assert`ed to 48 bytes so C++ and shader layouts cannot silently drift | compile |
| Config | `PreferredApi = Auto` is parsed and resolved from the loaded runtime (`vulkan-1.dll` / `d3d12.dll`) | compile, MinGW |
| Config | `JitterPattern = Halton` now warns and falls back instead of silently doing nothing | unit test |
| Jitter | `JitterScale` is honored | unit test + mutation test |
| Engine | **Frame-parity drift fixed:** only presents from the main output advance the frame counter; other swapchains (overlays, loading screens) are ignored | compile; logic review |
| Engine | `OnSwapchainRecreated()` resets parity and history; wired into the Vulkan swapchain hook | compile |
| Tests | New `tests/test_core.cpp` (config hardening, logger buffering and level, jitter, Apply/Remove inverse, VRAM = 268.95 MB, push-constant size). `-DCBR_BUILD_TESTS=ON` | passes in Release and Debug; **fails when code is deliberately broken**; clean under ASan + UBSan and `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` |
| CI | `.github/workflows/build.yml`: unit tests, shader validation, MSVC Release build, SHA-256 file | **not executed here; check on first push** |
| Docs | README (Testing section, new features), `CODEBASE.md` regenerated (round-trip verified, 29 files) | extraction diff |

---

## Part 2: P0, required before the mod can do anything in-game

### 2.1 Reconstruction cannot happen at Present time

This is the most important architectural point. The `OnPrePresent` hook runs when the frame is **already finished**: post-processing and UI are already applied. The reconstruction pass needs the **quarter-res 2×MSAA scene color/depth before tonemapping and UI**. Hooking Present is therefore suitable only for (a) frame counting, (b) overlay drawing and (c) swapchain-lifetime events.

Required instead:

1. Find the pass boundary mid-frame. Options: intercept render-target binding (`OMSetRenderTargets` / `vkCmdBeginRenderPass` / `vkCmdBeginRendering`), identify the scene HDR target by size, format and sample count (`RenderTargetManager::IsTargetInterceptCandidate` is the seed of this), and insert the compute pass at the end of that pass.
2. Alternatively research the engine's existing checkerboard path (`PostFX::g_CheckerBoardEnable`, referenced in CONTRIBUTING). Enabling or redirecting an existing engine path is far more robust than rebuilding the pass externally. This needs reverse-engineering, and no claim about its behavior is made here.

### 2.2 Real hooks (replace the honest stubs)

**Vulkan.** Hooking the *exported* `vkQueuePresentKHR` in `vulkan-1.dll` is not enough: games usually call device-level functions fetched through `vkGetDeviceProcAddr`, which can bypass the export. Hook the proc-address getters and hand back your own wrappers, or ship an implicit Vulkan layer (the sturdiest option; it coexists best with other tools).

```cpp
// Sketch only. MinHook API shown; verify against the version you vendor.
#include <MinHook.h>

static PFN_vkGetDeviceProcAddr g_origGetDeviceProcAddr = nullptr;

static PFN_vkVoidFunction VKAPI_CALL Hooked_vkGetDeviceProcAddr(VkDevice dev, const char* name) {
    PFN_vkVoidFunction real = g_origGetDeviceProcAddr(dev, name);
    if (!real) return nullptr;
    if (strcmp(name, "vkQueuePresentKHR") == 0) {
        g_realQueuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(real);
        return reinterpret_cast<PFN_vkVoidFunction>(&Hooked_vkQueuePresentKHR);
    }
    if (strcmp(name, "vkCreateSwapchainKHR") == 0) { /* same pattern */ }
    return real;
}

bool HookManager::InstallVulkanHooks() {
    HMODULE vk = GetModuleHandleA("vulkan-1.dll");
    if (!vk) return false;                       // caller must retry later (see 2.3)
    auto target = GetProcAddress(vk, "vkGetDeviceProcAddr");
    if (MH_CreateHook(target, &Hooked_vkGetDeviceProcAddr,
                      reinterpret_cast<void**>(&g_origGetDeviceProcAddr)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) return false;
    m_vulkanHooked.store(true);
    return true;
}
```

**DX12/DXGI.** Create a throwaway device and swapchain to read the swapchain vtable, then detour `Present` (index 8), `ResizeBuffers` (13) and, for flip-model games, `Present1` (22). Hook `ResizeBuffers` to call `OnSwapchainRecreated()` and release any buffers you hold, or the resize fails.

### 2.3 Deferred installation

`vulkan-1.dll` / `d3d12.dll` may load after the plugin's 1.5 s delay. Replace the fixed `Sleep(1500)` with a bounded retry loop (for example every 250 ms for 60 s) that stops once a hook installs. Log a single clear message if it times out.

### 2.4 Own your GPU resources

Nothing allocates GPU memory yet. Needed:

- Full-res **history color A/B** (`RGBA16F`), **history depth A/B** (`R32F`) and **output** (`RGBA16F`). Ping-pong both color *and* depth: the new depth output must become next frame's `u_HistoryDepth`.
- **Clear history to alpha = 0** on creation, resize, camera cut and swapchain recreation. The shader already treats `alpha <= 0` as "no history".
- Two descriptor sets (one per ping-pong index) so no descriptor updates happen mid-frame.
- Recreate everything on resolution change. Read the **actual** swapchain size, not `TargetWidth/Height` from the ini; a 1440p user with default config otherwise gets mismatched buffers.
- Vulkan barriers: `COLOR_ATTACHMENT_OUTPUT → COMPUTE_SHADER` before reading the MSAA target; `GENERAL` layout for storage images; `COMPUTE → FRAGMENT/TRANSFER` after. DX12: state transitions plus UAV barriers.
- Fence-protect anything the GPU may still be reading before you free or recreate it.

### 2.5 Don't crash on unknown game versions

RDR2 updates change addresses. Fingerprint the exe (version resource or hash), pattern-scan with fallbacks, and **refuse to inject** (log and exit cleanly) when anything fails to match. A mod that quietly does nothing is better than one that crashes the game.

---

## Part 3: P1, correctness of the algorithm

### 3.1 Jitter compensation (currently unused)

`JitterManager::GetJitterDelta()` is computed but never reaches the shader. For a static scene, derive the offset like this: if the current pixel `p` samples the scene point at `p + jc·s` and the history pixel `q` sampled it at `q + jp·s` (where `s = ±1` is the convention of how your projection jitter moves the sampled point), then matching the same scene point gives:

```
historyUV = uv + (jc - jp) * s - velocity
```

`s` depends on how the engine's matrix and clip-space Y convention are laid out, so determine it **empirically**: freeze the camera, set `DebugView` to the history-difference view, and flip `s` until static geometry stops shimmering. Add `jitterDelta` to the push constants (the struct is already `static_assert`-guarded, so add it deliberately) and ship a unit-testable function for the offset.

Also fix `Apply/RemoveJitterFromProjection`: they use `+=`/`-=`, so calling `Apply` twice in one frame doubles the jitter. Prefer `SetProjectionJitter(matrix, unjitteredMatrix)` or keep a per-matrix applied flag.

### 3.2 MSAA sample ↔ pixel mapping needs a real derivation

The shaders use `msaaSampleIndex = pixelCoord.x & 1` for every frame. That is only correct if the engine's 2× MSAA sample positions line up with that rule. With the standard 2× sample positions (diagonal: `(+¼,+¼)` and `(−¼,−¼)` from the pixel centre, in both D3D and Vulkan standard locations):

- In one 2×2 block, the two diagonal samples land on the **(0,0) and (1,1)** target pixels, which is the `(x+y)&1 == 0` set, and sample 0 maps to the **(1,1)** pixel, so `x & 1` has the sample indices swapped for this frame.
- The complementary diagonal `(1,0)/(0,1)` for odd frames is **not reachable by a ±0.5-target-pixel diagonal jitter**. A half *render*-pixel shift on one axis (= 1 target pixel) swaps the diagonal, but one of the two samples then belongs to the **neighbouring** render pixel.

So: confirm the sample positions the game actually uses (`VK_EXT_sample_locations` or the D3D12 programmable sample positions), then make the mapping a small, unit-tested function (`SampleForPixel(x, y, frameParity)` returning *(quarterCoord, sampleIndex)*) shared by clamp, fallback and resolve code, and re-derive the jitter amplitude from it. This is the single biggest correctness risk in the shader math, so treat it before tuning visuals.

### 3.3 Quality upgrades (once the above is right)

| Idea | Why |
|---|---|
| **Variance clipping** (μ ± γσ) instead of a min/max AABB | Less ghosting and less over-clamping; the standard modern TAA approach |
| **Closest-depth motion vector** from the 3×3 neighborhood | Cleaner moving-object edges |
| **Catmull-Rom (bicubic) history sampling** | Bilinear history causes progressive blur |
| **Blend in tonemapped/perceptual space**, un-tonemap after | HDR highlights otherwise dominate blends and flicker |
| **Motion-dependent history weight** | Lower `HistoryWeight` when velocity is large; fewer smear trails |
| **Relative depth weight** in the spatial fallback (replace `exp(-|Δz|·100)`) | The fixed ×100 is resolution- and depth-range-dependent; breaks with reversed-Z |
| **Optional sharpening pass** (CAS/RCAS-style) | Recovers detail lost to reconstruction |
| **Honor `MipLodBias`** | It is pushed to the shader but nothing consumes it; it belongs on the *scene* sampler during the quarter-res geometry pass, i.e. via the hooked sampler state, not in the compute shader |

### 3.4 Pascal performance

- **Shared-memory tiling** for the 3×3 neighborhood: load an 18×18 tile for each 16×16 group once. That cuts ~9 fetches per pixel to ≈1.3 and is the real win on a GTX 1070 Ti, whose bandwidth is the bottleneck. Remember the MSAA `texelFetch` cannot be shared directly; tile the resolved `YCoCg` values, not the MSAA texture.
- Pre-compute `1/targetResolution` (already done) and keep the early `isDisoccluded` exit before the clamp (already done).
- Measure with GPU timestamp queries and feed `GetLastReconstructionDurationMs()` (never set today) so the overlay can show it.

---

## Part 4: P1, robustness

- **Device loss / swapchain out-of-date** (`VK_ERROR_OUT_OF_DATE_KHR`, `DXGI_ERROR_DEVICE_REMOVED`): detect, disable CBR for the session, free resources, log once.
- **Alt-tab, minimize, fullscreen toggle, HDR, resolution change**: all trigger swapchain recreation; `OnSwapchainRecreated()` is the entry point, but resource recreation (2.4) must hang off it.
- **Fail-safe**: if any step fails, set `SetEnabled(false)` and let the game render normally. Never leave the game in a half-hooked state.
- **Crash visibility**: an optional unhandled-exception filter that writes a minidump next to the log, only when `LogToFile` is on.
- **Mod co-existence** (ScriptHookRDR2, ReShade, overlays): MinHook detours chain badly if two tools hook the same function. Prefer the Vulkan implicit-layer route and test with ReShade installed.

---

## Part 5: P2, security & supply chain

The risk surface is small (no network, no untrusted binary parsing). The remaining items are about distribution and process hygiene:

1. **Pin dependencies.** Vendor MinHook and ImGui as submodules at a **commit hash**, not a branch; record versions and licenses in `THIRD_PARTY_NOTICES.md` (MinHook is BSD-2, ImGui is MIT; confirm current terms).
2. **Release integrity.** CI now emits `SHA256SUMS.txt`. Next: Authenticode-sign the `.asi` if you can, and publish hashes on the release page. Name the exact `dinput8.dll` loader you recommend and link its official page with its hash.
3. **CFG / CET interplay.** The build enables `/guard:cf` and `/CETCOMPAT`. Inline-hook trampolines are not CFG-valid call targets and can fault under strict CET shadow-stack enforcement. Test on a CET-enabled machine, and be ready to drop `/CETCOMPAT` if hooks fault.
4. **Anti-cheat safety.** The policy is offline-only; enforce it. Before hooking, refuse to initialize if known online-service or anti-cheat modules are loaded. Build the module list from your own testing; none are asserted here.
5. **DLL search-order.** Any later `LoadLibrary` should use a full path or `LOAD_LIBRARY_SEARCH_SYSTEM32` so a planted DLL in the game folder cannot be loaded.
6. **Fuzz the ini parser.** A 30-line libFuzzer harness over `ConfigManager::Load` is cheap insurance; the parser already survives bad input in tests.
7. **Thread safety of config.** `GetMutableConfig()` is unsynchronized. When the overlay edits settings live, publish changes as an immutable snapshot (atomic `shared_ptr<const CBRConfig>`) and have the render thread read one snapshot per frame.

---

## Part 6: P2/P3, engineering hygiene & docs

- **CI hardening:** add `-Werror`, an ASan/UBSan job (verified locally here), `clang-tidy`, and `clang-format --dry-run`.
- **Warnings policy:** the portable code is already clean under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`; enforce it so it stays that way.
- **Docs:** add `docs/ARCHITECTURE.md` (frame flow with the corrected 2.1 timing diagram), `CHANGELOG.md`, and a **ban-risk and support** note in the README.
- **README honesty:** keep the `[x]/[ ]` list truthful as hooks land; add a "Known limitations" section that states plainly that no in-game integration exists yet.
- **Don't regress `CODEBASE.md`:** consider generating it from the tree in CI (the extraction script used for the round-trip check can be reversed) so it can never drift from the source again.

---

## Suggested order of work

1. **3.2** (sample mapping derivation) and **2.1** (where to inject), because they decide the whole design
2. **2.4** resources + **2.2** hooks, in a tiny "inject a debug color" proof-of-life
3. **3.1** jitter compensation, validated with the debug views
4. Quality (3.3) and Pascal tuning (3.4)
5. Release/security items (Part 5) before any public build
