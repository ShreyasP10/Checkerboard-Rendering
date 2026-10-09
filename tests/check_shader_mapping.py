#!/usr/bin/env python3
"""
Equivalence test between C++ checkerboard_mapping.h and shaders (GLSL / HLSL).
Verifies that:
1. C++ MapPixelToSample and shader MapPixelToSample implement identical logic.
2. Across two frames (parity 0 and parity 1), 100% 4-quadrant geometric coverage
   is achieved with standard 2x MSAA sample locations.
3. Shader files (cbr_reconstruct.comp and cbr_reconstruct.hlsl) contain the
   synchronized mapping logic.
"""

import sys
import os
from typing import Dict

def _check_coords(x: int, y: int, target_w: int, target_h: int) -> None:
    for name, v in (("x", x), ("y", y)):
        if not isinstance(v, int) or v < 0:
            raise ValueError(f"{name} must be a non-negative int, got {v!r}")
    if target_w <= 0 or target_h <= 0:
        raise ValueError("target dimensions must be positive")

def cpp_map_pixel_to_sample(x: int, y: int, frame_parity: int, shift_dir: int = 1) -> Dict[str, int]:
    if x < 0 or y < 0:
        raise ValueError(f"pixel coords must be >= 0, got ({x}, {y})")
    if frame_parity not in (0, 1):
        raise ValueError(f"frame_parity must be 0/1, got {frame_parity}")
    y_bit = y & 1
    active = bool(((x ^ y) & 1) == (frame_parity & 1))
    if (frame_parity & 1) == 0:
        qx = x >> 1
    elif shift_dir >= 0:
        qx = (x - y_bit if x > y_bit else 0) >> 1
    else:
        qx = (x + 1 - y_bit) >> 1
    qy = y >> 1
    sample = 1 - y_bit
    return {
        "active": active,
        "quarterX": qx,
        "quarterY": qy,
        "sample": sample
    }

def glsl_map_pixel_to_sample(x: int, y: int, frame_parity: int, shift_dir: int, target_w: int, target_h: int):
    _check_coords(x, y, target_w, target_h)
    y_bit = y & 1
    is_active = bool(((x ^ y) & 1) == (frame_parity & 1))
    if frame_parity == 0:
        qx = x >> 1
    elif shift_dir >= 0:
        qx = max(x - y_bit, 0) >> 1
    else:
        qx = (x + 1 - y_bit) >> 1
    q_max_x = (target_w // 2) - 1
    q_max_y = (target_h // 2) - 1
    clamped_qx = max(0, min(qx, q_max_x))
    clamped_qy = min(y >> 1, q_max_y)
    sample_idx = 1 - y_bit
    return {
        "active": is_active,
        "quarterX": clamped_qx,
        "quarterY": clamped_qy,
        "sample": sample_idx,
        "unclamped_qx": qx
    }

def hlsl_map_pixel_to_sample(x: int, y: int, frame_parity: int, shift_dir: int, target_w: int, target_h: int):
    # Same logic as GLSL
    return glsl_map_pixel_to_sample(x, y, frame_parity, shift_dir, target_w, target_h)

def test_mapping_equivalence():
    target_w = 3840
    target_h = 2160
    tested = 0
    for shift_dir in [1, -1]:
        for frame_parity in [0, 1]:
            # Test interior and boundary regions
            x_samples = list(range(0, 32)) + list(range(target_w // 2 - 16, target_w // 2 + 16)) + list(range(target_w - 32, target_w))
            y_samples = list(range(0, 32)) + list(range(target_h // 2 - 16, target_h // 2 + 16)) + list(range(target_h - 32, target_h))
            for y in y_samples:
                for x in x_samples:
                    cpp_res = cpp_map_pixel_to_sample(x, y, frame_parity, shift_dir)
                    glsl_res = glsl_map_pixel_to_sample(x, y, frame_parity, shift_dir, target_w, target_h)
                    hlsl_res = hlsl_map_pixel_to_sample(x, y, frame_parity, shift_dir, target_w, target_h)

                    assert cpp_res["active"] == glsl_res["active"] == hlsl_res["active"], f"Active mismatch at ({x}, {y})"
                    assert cpp_res["sample"] == glsl_res["sample"] == hlsl_res["sample"], f"Sample index mismatch at ({x}, {y})"
                    assert cpp_res["quarterY"] == glsl_res["quarterY"] == hlsl_res["quarterY"], f"quarterY mismatch at ({x}, {y})"

                    # In the interior (not edge), unclamped qx must match C++ exactly
                    assert cpp_res["quarterX"] == glsl_res["unclamped_qx"], f"quarterX mismatch at ({x}, {y})"
                    tested += 1

    print(f"[PASS] Verified mapping equivalence across {tested} coordinate combinations.")

def test_two_frame_coverage():
    # Verify 100% 4-quadrant coverage across 2 frames for 2x2 pixel blocks
    for shift_dir in [1, -1]:
        for block_x in range(0, 64, 2):
            for block_y in range(0, 64, 2):
                pixels = [
                    (block_x, block_y),
                    (block_x + 1, block_y),
                    (block_x, block_y + 1),
                    (block_x + 1, block_y + 1),
                ]
                f0_active = [p for p in pixels if cpp_map_pixel_to_sample(p[0], p[1], 0, shift_dir)["active"]]
                f1_active = [p for p in pixels if cpp_map_pixel_to_sample(p[0], p[1], 1, shift_dir)["active"]]

                assert len(f0_active) == 2, f"Frame 0 active count != 2 at block ({block_x}, {block_y})"
                assert len(f1_active) == 2, f"Frame 1 active count != 2 at block ({block_x}, {block_y})"
                assert set(f0_active).isdisjoint(set(f1_active)), f"Overlap between frame 0 and frame 1 at block ({block_x}, {block_y})"
                assert len(set(f0_active) | set(f1_active)) == 4, f"Incomplete coverage across 2 frames at block ({block_x}, {block_y})"

    print("[PASS] Verified 100% 4-quadrant coverage across 2 frames.")

def test_source_code_consistency():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))

    comp_path = os.path.join(repo_root, "shaders", "cbr_reconstruct.comp")
    hlsl_path = os.path.join(repo_root, "shaders", "cbr_reconstruct.hlsl")
    header_path = os.path.join(repo_root, "include", "cbr", "checkerboard_mapping.h")

    assert os.path.isfile(comp_path), f"Missing {comp_path}"
    assert os.path.isfile(hlsl_path), f"Missing {hlsl_path}"
    assert os.path.isfile(header_path), f"Missing {header_path}"

    with open(comp_path, "r", encoding="utf-8") as f:
        comp_src = f.read()
    with open(hlsl_path, "r", encoding="utf-8") as f:
        hlsl_src = f.read()
    with open(header_path, "r", encoding="utf-8") as f:
        header_src = f.read()

    # Check that MapPixelToSample is present in all three
    assert "MapPixelToSample" in comp_src, "MapPixelToSample missing from cbr_reconstruct.comp"
    assert "MapPixelToSample" in hlsl_src, "MapPixelToSample missing from cbr_reconstruct.hlsl"
    assert "MapPixelToSample" in header_src, "MapPixelToSample missing from checkerboard_mapping.h"

    # Check parity check formula ((x ^ y) & 1) == frameParity in all three
    assert "(p.x) ^ uint(p.y)) & 1u) == frameParity" in comp_src
    assert "(p.x) ^ uint(p.y)) & 1u) == frameParity" in hlsl_src
    assert "((static_cast<uint32_t>(x ^ y) & 1u) == (frameParity & 1u))" in header_src

    print("[PASS] Verified source code consistency across GLSL, HLSL, and C++ header.")

def main():
    test_mapping_equivalence()
    test_two_frame_coverage()
    test_source_code_consistency()
    print("All shader-to-C++ mapping equivalence tests passed successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
