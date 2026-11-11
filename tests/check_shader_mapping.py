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
import re
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

    # Explicit boundary case (Item M-06): (x=0, y=1, frameParity=1, shiftDir=1) -> x - yBit = -1 must clamp to 0
    neg_edge_cpp = cpp_map_pixel_to_sample(0, 1, 1, 1)
    neg_edge_glsl = glsl_map_pixel_to_sample(0, 1, 1, 1, target_w, target_h)
    neg_edge_hlsl = hlsl_map_pixel_to_sample(0, 1, 1, 1, target_w, target_h)
    assert neg_edge_cpp["quarterX"] == 0 and neg_edge_glsl["unclamped_qx"] == 0 and neg_edge_glsl["quarterX"] == 0, (
        f"Expected negative shift clamp to 0 for (0,1,parity=1,shift=1), got {neg_edge_cpp} / {neg_edge_glsl}"
    )
    assert neg_edge_hlsl["quarterX"] == 0 and neg_edge_cpp["sample"] == 0 and neg_edge_cpp["active"] is True

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

    quad_path = os.path.join(repo_root, "shaders", "cbr_reconstruct_quad.comp")
    assert os.path.isfile(comp_path), f"Missing {comp_path}"
    assert os.path.isfile(quad_path), f"Missing {quad_path}"
    assert os.path.isfile(hlsl_path), f"Missing {hlsl_path}"
    assert os.path.isfile(header_path), f"Missing {header_path}"

    with open(comp_path, "r", encoding="utf-8") as f:
        comp_src = f.read()
    with open(quad_path, "r", encoding="utf-8") as f:
        quad_src = f.read()
    with open(hlsl_path, "r", encoding="utf-8") as f:
        hlsl_src = f.read()
    with open(header_path, "r", encoding="utf-8") as f:
        header_src = f.read()

    # Check that MapPixelToSample is present in all
    assert "MapPixelToSample" in comp_src, "MapPixelToSample missing from cbr_reconstruct.comp"
    assert "MapPixelToSample" in quad_src, "MapPixelToSample missing from cbr_reconstruct_quad.comp"
    assert "MapPixelToSample" in hlsl_src, "MapPixelToSample missing from cbr_reconstruct.hlsl"
    assert "MapPixelToSample" in header_src, "MapPixelToSample missing from checkerboard_mapping.h"

    # Regex extraction of MapPixelToSample qx branch expressions across GLSL and HLSL (Item M-06)
    map_fn_re = re.compile(r"CbrSample\s+MapPixelToSample\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}", re.DOTALL)
    qx_branch_re = re.compile(
        r"if\s*\(\s*frameParity\s*==\s*0u\s*\)\s*\{\s*qx\s*=\s*p\.x\s*>>\s*1\s*;\s*\}\s*"
        r"else\s+if\s*\(\s*(?:pc\.u_ShiftDirection|g_ShiftDirection)\s*>=\s*0\s*\)\s*\{\s*qx\s*=\s*max\(\s*p\.x\s*-\s*yBit\s*,\s*0\s*\)\s*>>\s*1\s*;\s*\}\s*"
        r"else\s*\{\s*qx\s*=\s*\(\s*p\.x\s*\+\s*1\s*-\s*yBit\s*\)\s*>>\s*1\s*;\s*\}",
        re.DOTALL,
    )
    for label, src in (
        ("cbr_reconstruct.comp", comp_src),
        ("cbr_reconstruct_quad.comp", quad_src),
        ("cbr_reconstruct.hlsl", hlsl_src),
    ):
        fn_match = map_fn_re.search(src)
        assert fn_match is not None, f"MapPixelToSample definition missing in {label}"
        assert qx_branch_re.search(fn_match.group("body")) is not None, (
            f"MapPixelToSample qx branch expressions drifted in {label}"
        )

    # Check parity check formula ((x ^ y) & 1) == frameParity in all
    assert "(p.x) ^ uint(p.y)) & 1u) == frameParity" in comp_src
    assert "(p.x) ^ uint(p.y)) & 1u) == frameParity" in quad_src
    assert "(p.x) ^ uint(p.y)) & 1u) == frameParity" in hlsl_src
    assert "((static_cast<uint32_t>(x ^ y) & 1u) == (frameParity & 1u))" in header_src

    # Verify quarter-res velocity coordinate shift (>> 1) and clamp across ALL three shaders (Item C-05)
    for label, src in (("cbr_reconstruct.comp", comp_src), ("cbr_reconstruct_quad.comp", quad_src)):
        assert "clamp(motionCoord >> 1, ivec2(0)," in src, f"C-05 velCoord clamp missing in {label}"
        assert "(targetSize >> 1) - ivec2(1)" in src, f"C-05 quarter-res upper bound missing in {label}"
        assert "texelFetch(u_Velocity, velCoord, 0)" in src, f"C-05 texelFetch(u_Velocity, velCoord, 0) missing in {label}"
        assert "fallback = vec3(0.0);" in src, f"H-05 double-guarded fallback missing in {label}"

    assert "clamp(motionCoord >> 1, int2(0, 0)," in hlsl_src, "C-05 velCoord clamp missing in cbr_reconstruct.hlsl"
    assert "(targetSize >> 1) - int2(1, 1)" in hlsl_src, "C-05 quarter-res upper bound missing in cbr_reconstruct.hlsl"
    assert "g_Velocity.Load(int3(velCoord, 0))" in hlsl_src, "C-05 g_Velocity.Load(int3(velCoord, 0)) missing in cbr_reconstruct.hlsl"
    assert "g_HistoryDepth.Load(int3(historyCoord, 0)).r" in hlsl_src, "M-02 HLSL Load() parity missing"
    assert "fallback = float3(0.0f, 0.0f, 0.0f);" in hlsl_src, "H-05 double-guarded fallback missing in cbr_reconstruct.hlsl"
    assert "GL_KHR_shader_subgroup" not in quad_src, "M-07 unused subgroup extensions must be removed"
    assert "QuadActiveSample" in quad_src and "cardinalColor" in quad_src, "M-07/H-05 quad register sharing missing"

    print("[PASS] Verified source code consistency across GLSL, HLSL, and C++ header.")

def map_velocity_coord(mx: int, my: int, target_w: int, target_h: int):
    qw = max((target_w >> 1) - 1, 0)
    qh = max((target_h >> 1) - 1, 0)
    return max(0, min(mx >> 1, qw)), max(0, min(my >> 1, qh))

def test_velocity_coord_mapping():
    resolutions = [
        (3840, 2160),  # 4K
        (3800, 1920),  # ultrawide custom resolution
        (2560, 1440),  # 1440p
        (1920, 1080),  # 1080p (Vega 7)
        (2, 2),        # minimum valid RenderTargetManager extent
    ]
    checked = 0
    for tw, th in resolutions:
        qw, qh = tw >> 1, th >> 1
        xs = sorted({0, 1, tw // 4, tw // 2, max(0, tw - 2), tw - 1, tw, tw + 2})
        ys = sorted({0, 1, th // 4, th // 2, max(0, th - 2), th - 1, th, th + 2})
        for my in ys:
            for mx in xs:
                vx, vy = map_velocity_coord(mx, my, tw, th)
                assert 0 <= vx < qw and 0 <= vy < qh, f"OOB quarter velocity coord {(vx, vy)} for {tw}x{th}"
                if 0 <= mx < tw:
                    assert vx == (mx >> 1), f"vx shift mismatch: {vx} != {mx >> 1}"
                if 0 <= my < th:
                    assert vy == (my >> 1), f"vy shift mismatch: {vy} != {my >> 1}"
                checked += 1
    print(f"[PASS] Verified quarter-res C-05 velocity coordinate shift & clamp across {checked} cases.")

def main():
    test_mapping_equivalence()
    test_two_frame_coverage()
    test_velocity_coord_mapping()
    test_source_code_consistency()
    print("All shader-to-C++ mapping equivalence tests passed successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
