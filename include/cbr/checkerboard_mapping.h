#pragma once

#include <cstdint>

namespace cbr {

// -----------------------------------------------------------------------------------------------
// Checkerboard sample geometry for 2x MSAA at half resolution per axis ("quarter" resolution).
//
// Standard 2x MSAA sample locations in [0,1] pixel coordinates, y DOWN (the Vulkan "standard
// sample locations", also used by D3D's standard pattern and by Metal):
//   sample 0 = (0.75, 0.75) bottom-right
//   sample 1 = (0.25, 0.25) top-left
// Sources: Mesa src/intel/common/intel_sample_positions.* ("standard multisample positions used
// by both GL and Vulkan") and Apple's MTLDevice.getDefaultSamplePositions(2). Confirm against the
// sample locations the game actually uses (VK_EXT_sample_locations / D3D12 programmable positions).
//
// One render pixel covers a 2x2 block of output ("target") pixels, so the two samples land on the
// centres of the top-left and bottom-right target pixels of that block.
//
// frameParity 0 (unjittered): samples shade target pixels with (x&1) == (y&1).
// frameParity 1 (sampling grid shifted by shiftDir full-resolution pixels in +x):
//   samples shade target pixels with (x&1) != (y&1).
//
// Across the two frames every target pixel is shaded exactly once (verified by simulation in
// tests/test_core.cpp). The jitter is a whole-pixel coverage shift, NOT sub-pixel TAA jitter: the
// mapping below already assigns each sample to the output pixel it belongs to, so history
// reprojection needs no jitter compensation.
// -----------------------------------------------------------------------------------------------

struct SampleOffset {
    float x;
    float y;
};

inline constexpr SampleOffset kStandardSample2x[2] = {
    { 0.75f, 0.75f },
    { 0.25f, 0.25f }
};

struct CheckerboardSample {
    bool     active;   // true if a native sample exists for this target pixel this frame
    int32_t  quarterX; // render-target (quarter-res) texel
    int32_t  quarterY;
    uint32_t sample;   // MSAA sample index within that texel
};

// Maps a target pixel (x, y >= 0) to the MSAA sample that shaded it.
// For inactive pixels the returned location is the (meaningless) sample that would be there if
// the pixel were active; callers must check `active`.
// quarterX may fall outside the render target at the left/right screen edge; callers must clamp.
constexpr CheckerboardSample MapPixelToSample(int32_t x, int32_t y, uint32_t frameParity, int32_t shiftDir = 1) noexcept {
    const int32_t yBit = y & 1;
    const bool active = ((static_cast<uint32_t>(x ^ y) & 1u) == (frameParity & 1u));
    int32_t qx = 0;
    if ((frameParity & 1u) == 0u) {
        qx = x >> 1;
    } else if (shiftDir >= 0) {
        // +1 px shift: the bottom-right sample of the neighbouring render pixel lands here
        qx = (x > yBit ? x - yBit : 0) >> 1;
    } else {
        // -1 px shift
        qx = (x + 1 - yBit) >> 1;
    }
    return { active, qx, y >> 1, static_cast<uint32_t>(1 - yBit) };
}

} // namespace cbr
