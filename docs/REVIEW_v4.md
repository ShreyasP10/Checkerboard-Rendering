# Review of your latest update (v3 → v4)

Scope: the 11 files that changed since the last version. Everything here was checked by compiling (g++ and MinGW-w64, `-Wall -Wextra -Wpedantic -Wshadow`), validating the shaders (glslang; HLSL through its HLSL front-end, not full `dxc`), running the unit tests (Release, Debug, AddressSanitizer + UBSan, and a ThreadSanitizer race test), and a full cross-build. **Not verified:** MSVC-specific flags, a real `dxc` build, and anything inside the game.

## What you did well

| Change | Verdict |
|---|---|
| `OnScenePassEnd` mid-frame injection; `OnPrePresent` reduced to overlay only | Correct architecture (reconstruction must happen before post-processing and UI) |
| `SetProjectionJitter` (non-accumulating) | Good; matches `ApplyJitterToProjection` exactly (now tested) |
| Jitter delta added to push constants, with a `static_assert` on the 64-byte layout | Good; layout verified against the GLSL block and HLSL cbuffer |
| Closest-depth motion dilation, variance clipping, relative depth weight | Sound techniques; see the notes below |
| Retry loop instead of a fixed `Sleep(1500)` | Right idea; see bug 2 below, because it retried the wrong thing |
| README install and troubleshooting walkthrough | Useful; see the accuracy fixes below |

## Bugs found in the new code, and fixed in v4

### 1. `OnScenePassEnd` could swap history several times per frame (High)
The callback had no once-per-frame guard. A frame can contain several matching passes (reflections, mirrors, cubemaps), and every one ran a dispatch **and** flipped the history ping-pong, which desynchronizes history from the frame it belongs to.
**Fix:** a lock-free `exchange()` guard so reconstruction runs at most once per frame, reset on swapchain recreation. A ThreadSanitizer test with 4 threads racing in one frame confirms exactly one swap.

### 2. The retry loop retried the trigger, not the hook installation (High)
`CBRInitThread` waited for `vulkan-1.dll` **or** `d3d12.dll`, then called `Initialize()` once. Initialization is `call_once`, so if the configured API's runtime wasn't loaded yet (for example `d3d12.dll` appeared first while the config says Vulkan), the install failed and was **never attempted again**. The 60-second timeout fallback had the same flaw.
**Fix:** `Initialize()` now does only the runtime-independent setup, and the new `TryInstallHooks()` is safe to call repeatedly. The thread initializes immediately, then polls `TryInstallHooks()` every 250 ms for up to 60 s. `PreferredApi = Auto` re-detects on each attempt, and the install is serialized with a mutex so `CBR_PluginInit` and the init thread cannot race. The still-unimplemented hook stubs now warn once, instead of 240 times.

### 3. Jitter sign convention was hard-coded and unverified (Medium)
`historyUV = uv - velocity - jitterDelta` fixes one sign without any evidence. The correct sign depends on the engine's projection convention.
**Fix:** `JitterCompensation` (config, range −1 to 1, default 1) multiplies the delta: 1 as you wrote it, −1 to flip, 0 to disable. Confirm it in-game with a static camera: the right value makes static geometry stop shimmering.

### 4. Motion dilation was unconditional: 9 extra MSAA fetches per pixel (Medium, performance)
At 4K that is roughly 75 million extra fetches per frame, on exactly the bandwidth-limited Pascal target, and it undid the earlier "guard the expensive loop" work.
**Fix:** `EnableMotionDilation` (default on) skips the loop entirely when off. A longer-term fix is a shared-memory depth tile (see `IMPROVEMENTS.md` 3.4).

### 5. Duplicated push-constant code (Low)
`DispatchVulkan` and `DispatchDX12` each built the payload by hand, so the two could drift apart. **Fix:** one `BuildReconstructionPushConstants()`, now unit-tested.

### 6. `SetProjectionJitter` aliasing footgun (Low)
Passing the same pointer for input and output and calling twice re-applies the offset, defeating the point of the function. Documented in the header; the test covers the non-aliased use.

### 7. README accuracy (Medium)
- It described install and in-game settings as if the mod worked. **It cannot yet: the hooks are not implemented.** Added a clear status note and a note on the settings section.
- "Ensure the Visual C++ Redistributable is installed": the build uses a static CRT, so that is unnecessary. Corrected.
- The ASI-loader link used plain `http://`. Changed to `https://` with a checksum reminder.
- `#configuration` in the table of contents pointed at a non-existent anchor (the heading is "Configuration (`cbr.ini`)"). Fixed to `#configuration-cbrini`.
- Statements presented as fact that are unverified (velocity vectors only exist with TAA, MSAA behavior, "fits comfortably in 8 GB", Async Compute benefits, `MipLodBias` preserving sharpness, which is **not applied anywhere yet**) were reworded as assumptions or marked reserved.
- New options documented. All three example configs (two in the README, plus `cbr.ini`) were extracted and loaded through the real parser: no warnings, correct values.

## Still open (unchanged from the roadmap)

- **Hooks are still stubs.** Nothing calls `OnScenePassEnd`, `OnBeginFrame` or `OnSwapchainRecreated` from a real game yet.
- **MSAA sample ↔ pixel mapping.** The new dilation loop also uses `x & 1` as the sample index. See `IMPROVEMENTS.md` 3.2: this needs a real derivation from the engine's sample positions before visual tuning means anything.
- **Reversed-Z.** The closest-depth comparison assumes smaller = nearer (commented in the shader). Relative depth on non-linear depth is not truly range-independent; linearize depth for that claim to hold.
- **Variance clipping `gamma = 1.25`** is hard-coded; consider making it configurable, and clip in tonemapped space for HDR.
- Everything in Parts 2 to 6 of `IMPROVEMENTS.md` that is not marked implemented.

## New tests (all pass; verified to fail when the bugs are reintroduced)

| Test | Catches |
|---|---|
| Retry semantics (3 failures, then success, then no further installs) | The retry-loop bug and double installs |
| Duplicate `OnScenePassEnd` in one frame; next frame swaps again | Missing once-per-frame guard (mutation-tested; also fixed a weak first draft that passed by parity luck) |
| Parity advances only for the main present target | Frame-parity drift (mutation-tested) |
| Swapchain recreation resets parity, history, guard | Stale-state bugs |
| `Auto` with no runtime loaded: zero install attempts | Premature or wrong-API hooks |
| Push-constant builder reflects config and jitter state | Config/shader plumbing drift |
| `SetProjectionJitter` equals `Apply`, leaves its source untouched | Matrix-offset regressions |
| New config keys: parsing, clamping, save/load round-trip | Parser regressions |
