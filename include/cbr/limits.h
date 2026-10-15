#pragma once

#include <cstddef>
#include <cstdint>

namespace cbr {

// Central dimension, buffer, and configuration bounds
constexpr uint32_t kMinTargetWidth = 720;
constexpr uint32_t kMaxTargetWidth = 7680;
constexpr uint32_t kMinTargetHeight = 480;
constexpr uint32_t kMaxTargetHeight = 4320;

constexpr uint32_t kMinSwapchainExtent = 320;
constexpr uint32_t kMaxSwapchainExtent = 16384;

constexpr uint32_t kWorkgroupSizeX = 16;
constexpr uint32_t kWorkgroupSizeY = 16;

constexpr size_t kMaxConfigFileBytes = 64 * 1024;
constexpr size_t kMaxConfigLines = 500;
constexpr size_t kMaxConfigLineChars = 1024;

constexpr size_t kMaxLogLineChars = 1024;

constexpr float kVarianceClipGamma = 1.25f;

inline uint32_t MakeEvenUp(uint32_t v) { return (v & 1u) ? v + 1u : v; }

} // namespace cbr
