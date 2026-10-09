#include "cbr/checkerboard_mapping.h"
#include "cbr/config.h"
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace cbr;

static int g_failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL: " #expr " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failures; \
    } \
} while (0)

static void WriteFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::out | std::ios::trunc);
    f << content;
    f.flush();
    if (!f.good()) {
        std::cerr << "FAIL: WriteFile could not write " << p << "\n";
        ++g_failures;
    }
}

static std::string ReadFile(const fs::path& p) {
    std::ifstream f(p);
    if (!f.is_open()) {
        std::cerr << "FAIL: ReadFile could not open " << p << "\n";
        ++g_failures;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void TestConfigHardening(const fs::path& dir) {
    const fs::path ini = dir / "bad.ini";
    WriteFile(ini,
        "[General]\n"
        "TargetWidth = 4k\n"          // trailing garbage  -> default
        "TargetHeight = -1\n"         // negative          -> default
        "Enabled = true ; comment\n"  // inline comment
        "PreferredApi = Auto\n"
        "[Reconstruction]\n"
        "EnableMotionDilation = false\n"
        "DepthConvention = standard\n"   // case-insensitive
        "DepthNear = 0.5\n"
        "DepthFar = 5000\n"
        "HistoryWeight = nan\n"       // NaN               -> default
        "DepthTolerance = 99999\n"    // out of range      -> clamped
        "MipLodBias = abc\n"          // garbage           -> default
        "[Jitter]\n"
        "JitterPattern = Halton\n"    // unimplemented     -> Checkerboard
        "JitterCompensation = -5\n"   // out of range      -> clamped to -1
        "JitterDirection = -1\n"
        "ProjectionJitterSign = -1\n"
        "JitterScale = 3\n"              // accepted but ignored (warned)
        "[Debug]\n"
        "DebugView = 77\n");          // out of range      -> clamped

    CHECK(ConfigManager::Get().Load(ini));
    const auto& c = ConfigManager::Get().GetConfig();
    CHECK(c.targetWidth == 3840);
    CHECK(c.targetHeight == 2160);
    CHECK(c.enabled);
    CHECK(c.preferredApi == GraphicsApi::Auto);
    CHECK(c.historyWeight == 0.90f);
    CHECK(c.depthTolerance == 1.0f);
    CHECK(c.mipLodBias == -0.5f);
    CHECK(c.jitterPattern == JitterPattern::Checkerboard);
    CHECK(c.debugView == 5);
    CHECK(!c.enableMotionDilation);
    CHECK(c.jitterCompensation == -1.0f);
    CHECK(c.jitterDirection == -1);
    CHECK(c.projectionJitterSign == -1);
    CHECK(c.depthConvention == DepthConvention::Standard);
    CHECK(c.depthNear == 0.5f && c.depthFar == 5000.0f);

    // Defaults of a fresh config
    const CBRConfig fresh{};
    CHECK(fresh.jitterCompensation == 0.0f); // whole-pixel jitter needs NO history compensation
    CHECK(fresh.jitterDirection == 1);
    CHECK(fresh.projectionJitterSign == 1);
    CHECK(fresh.depthConvention == DepthConvention::Reversed);

    // Invalid enum-like values keep the previous setting
    WriteFile(dir / "bad2.ini", "[Jitter]\nJitterDirection = 2\nProjectionJitterSign = 42\n[Reconstruction]\nDepthConvention = sideways\n");
    CHECK(ConfigManager::Get().Load(dir / "bad2.ini"));
    CHECK(ConfigManager::Get().GetConfig().jitterDirection == -1);
    CHECK(ConfigManager::Get().GetConfig().projectionJitterSign == -1);
    CHECK(ConfigManager::Get().GetConfig().depthConvention == DepthConvention::Standard);
}

static void TestConfigMissingFileAndRoundTrip(const fs::path& dir) {
    CHECK(!ConfigManager::Get().Load(dir / "does_not_exist.ini"));

    const fs::path out = dir / "roundtrip.ini";
    CHECK(ConfigManager::Get().Save(out));
    const auto before = ConfigManager::Get().GetConfig();
    CHECK(ConfigManager::Get().Load(out));
    const auto& after = ConfigManager::Get().GetConfig();
    CHECK(after.targetWidth == before.targetWidth);
    CHECK(after.preferredApi == before.preferredApi);
    CHECK(after.historyWeight == before.historyWeight);
    CHECK(after.enableMotionDilation == before.enableMotionDilation);
    CHECK(after.jitterCompensation == before.jitterCompensation);
    CHECK(after.jitterDirection == before.jitterDirection);
    CHECK(after.projectionJitterSign == before.projectionJitterSign);
    CHECK(after.depthConvention == before.depthConvention);
    CHECK(after.depthNear == before.depthNear && after.depthFar == before.depthFar);
}

static void TestLoggerBufferingAndLevel(const fs::path& dir) {
    // Messages logged before Initialize() must be buffered and flushed, honoring the min level.
    Logger::Get().SetMinLevel(LogLevel::Info);
    CBR_LOG_INFO("early info %d", 1);
    Logger::Get().SetMinLevel(LogLevel::Warning);
    CBR_LOG_INFO("filtered info");
    CBR_LOG_WARN("late warning %d", 2);

    const fs::path log = dir / "test.log";
    Logger::Get().Initialize(log);
    Logger::Get().Shutdown();

    const std::string text = ReadFile(log);
    CHECK(text.find("early info 1") != std::string::npos);   // buffered before init
    CHECK(text.find("late warning 2") != std::string::npos);
    CHECK(text.find("filtered info") == std::string::npos);  // below min level
}

static void TestJitter() {
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 1.0f; c.jitterDirection = 1; });
    auto& j = JitterManager::Get();
    j.Initialize(3840, 2160);

    j.Update(0);
    const JitterOffset even = j.GetCurrentJitter();
    j.Update(1);
    const JitterOffset odd  = j.GetCurrentJitter();

    // Even frame is unjittered (samples on pixel centres); odd frame is shifted by exactly ONE full-res pixel in x.
    CHECK(even.x == 0.0f && even.y == 0.0f);
    CHECK(std::fabs(odd.x - 1.0f / 3840.0f) < 1e-12f);
    CHECK(odd.y == 0.0f); // horizontal only: a diagonal shift covers just half the pixels
    CHECK(std::fabs(j.GetJitterDelta().x - (odd.x - even.x)) < 1e-12f);

    // JitterScale must NOT change the geometry (coverage needs exactly one pixel)
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 2.0f; });
    j.Update(1);
    CHECK(std::fabs(j.GetCurrentJitter().x - 1.0f / 3840.0f) < 1e-12f);
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterScale = 1.0f; });

    // JitterDirection flips the shift
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = -1; });
    j.Update(1);
    CHECK(std::fabs(j.GetCurrentJitter().x + 1.0f / 3840.0f) < 1e-12f);
    j.Update(0);
    CHECK(j.GetCurrentJitter().x == 0.0f);
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = 1; });
    j.Update(1);

    // Apply/Remove must be exact inverses
    float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0.25f, 0.5f, 1, 0,  0, 0, 0, 1 };
    float orig[16];
    std::copy(m, m + 16, orig);
    j.ApplyJitterToProjection(m, true);
    j.RemoveJitterFromProjection(m, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(m[i] - orig[i]) < 1e-7f);

    // SetProjectionJitter must be idempotent without compounding offsets
    float out1[16];
    float out2[16];
    float snapshot[16];
    std::copy(orig, orig + 16, snapshot);
    j.SetProjectionJitter(out1, orig, true);
    j.SetProjectionJitter(out2, orig, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(out1[i] - out2[i]) < 1e-7f);

    // ...and must produce exactly what Apply produces from the same unjittered matrix
    float viaApply[16];
    std::copy(orig, orig + 16, viaApply);
    j.ApplyJitterToProjection(viaApply, true);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(out1[i] - viaApply[i]) < 1e-7f);

    // Non-aliased output leaves the unjittered source untouched
    for (int i = 0; i < 16; ++i) CHECK(orig[i] == snapshot[i]);
}

// Geometric simulation of 2x MSAA checkerboard coverage, driven by the REAL JitterManager output.
// Independent of MapPixelToSample's closed form: positions come from the standard sample locations
// (sample 0 = (0.75,0.75), sample 1 = (0.25,0.25), y down) plus the jitter, and the pixel containing
// each sample position is found with floor().
static void TestCheckerboardGeometry() {
    constexpr int kW = 64, kH = 36; // target size (render target = 32 x 18)
    constexpr int kQW = kW / 2, kQH = kH / 2;

    for (int dir : { 1, -1 }) {
        ConfigManager::Get().Modify([dir](CBRConfig& c) { c.jitterDirection = dir; });
        JitterManager::Get().Initialize(kW, kH);

        int coverage[kH][kW] = {}; // how many frames natively shaded each pixel
        for (uint32_t parity = 0; parity < 2; ++parity) {
            JitterManager::Get().Update(parity);
            const double shiftPx = static_cast<double>(JitterManager::Get().GetCurrentJitter().x) * kW;
            for (int qy = 0; qy < kQH; ++qy) {
                for (int qx = 0; qx < kQW; ++qx) {
                    for (uint32_t s = 0; s < 2; ++s) {
                        const double px = 2.0 * (qx + kStandardSample2x[s].x) + shiftPx; // target-pixel units
                        const double py = 2.0 * (qy + kStandardSample2x[s].y);
                        const int tx = static_cast<int>(std::floor(px));
                        const int ty = static_cast<int>(std::floor(py));
                        // Every sample sits exactly on a pixel CENTRE (fractional part 0.5)
                        CHECK(std::fabs((px - std::floor(px)) - 0.5) < 1e-4);
                        CHECK(std::fabs((py - std::floor(py)) - 0.5) < 1e-4);
                        if (tx < 0 || tx >= kW || ty < 0 || ty >= kH) continue;
                        ++coverage[ty][tx];

                        const CheckerboardSample m = MapPixelToSample(tx, ty, parity, dir);
                        CHECK(m.active);
                        CHECK(m.quarterX == qx && m.quarterY == qy && m.sample == s);
                    }
                }
            }
        }
        // Over two frames every interior pixel is natively shaded exactly once (the checkerboard is complete)
        int wrong = 0;
        for (int y = 0; y < kH; ++y) {
            for (int x = 2; x < kW - 2; ++x) {
                if (coverage[y][x] != 1) ++wrong;
            }
        }
        CHECK(wrong == 0);

        // Parity bookkeeping: exactly half of all pixels are active in each frame
        for (uint32_t parity = 0; parity < 2; ++parity) {
            int active = 0;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    if (MapPixelToSample(x, y, parity, dir).active) ++active;
            CHECK(active == kW * kH / 2);
        }
    }
    ConfigManager::Get().Modify([](CBRConfig& c) { c.jitterDirection = 1; });

    // Negative controls: the previous schemes must be rejected by the same simulation
    auto covered = [&](double sx0, double sy0, double sx1, double sy1) {
        bool seen[kH][kW] = {};
        const double shifts[2][2] = { { sx0, sy0 }, { sx1, sy1 } };
        for (const auto& sh : shifts)
            for (int qy = 0; qy < kQH; ++qy)
                for (int qx = 0; qx < kQW; ++qx)
                    for (const auto& so : kStandardSample2x) {
                        const int tx = static_cast<int>(std::floor(2.0 * (qx + so.x) + sh[0]));
                        const int ty = static_cast<int>(std::floor(2.0 * (qy + so.y) + sh[1]));
                        if (tx >= 0 && tx < kW && ty >= 0 && ty < kH) seen[ty][tx] = true;
                    }
        int n = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 2; x < kW - 2; ++x)
                n += seen[y][x] ? 1 : 0;
        return n;
    };
    const int interior = kH * (kW - 4);
    CHECK(covered(0, 0, 1, 0) == interior);                    // Intel scheme: complete
    CHECK(covered(-0.5, 0, 0.5, 0) == interior);              // symmetric +/-0.5: complete (but samples on pixel edges)
    CHECK(covered(-0.5, -0.5, 0.5, 0.5) == interior / 2);     // old diagonal scheme: only half the pixels, ever
}

static void TestDepthConvention() {
    // Non-finite device depth must poison (NaN) and always fail the tolerance test.
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        CHECK(!std::isfinite(LinearizeDepth(nan, DepthConvention::Reversed, 0.1f, 0.0f)));
        CHECK(!std::isfinite(LinearizeDepth(inf, DepthConvention::Standard, 0.1f, 1000.0f)));
        CHECK(RelativeDepthDelta(nan, 1.0f) > 1.0f);
        CHECK(RelativeDepthDelta(1.0f, inf) > 1.0f);
    }
    // Round trip: project a view-space distance to device depth, linearize it back.
    for (double z : { 0.2, 1.0, 10.0, 100.0, 1000.0, 5000.0 }) {
        const double n = 0.1, f = 10000.0;
        const double dStd = f * (z - n) / (z * (f - n));  // standard: 0 near .. 1 far
        const double dRev = 1.0 - dStd;                   // reversed: 1 near .. 0 far
        const double dInf = n / z;                        // reversed, infinite far plane

        const float zStd = LinearizeDepth(static_cast<float>(dStd), DepthConvention::Standard, 0.1f, 10000.0f);
        const float zRev = LinearizeDepth(static_cast<float>(dRev), DepthConvention::Reversed, 0.1f, 10000.0f);
        const float zInf = LinearizeDepth(static_cast<float>(dInf), DepthConvention::Reversed, 0.1f, 0.0f);

        // float32 device depth limits precision (esp. standard-Z far away), so allow a tolerance
        CHECK(std::fabs(zRev - z) / z < 5e-3);
        CHECK(std::fabs(zInf - z) / z < 1e-4);
        if (z <= 100.0) CHECK(std::fabs(zStd - z) / z < 5e-3);
    }

    // Scale invariance: a 1% depth change gives the same relative delta at 1 m and at 1 km (reversed, infinite far)
    auto relDelta = [](double z) {
        const double n = 0.1;
        const float a = LinearizeDepth(static_cast<float>(n / z), DepthConvention::Reversed, 0.1f, 0.0f);
        const float b = LinearizeDepth(static_cast<float>(n / (z * 1.01)), DepthConvention::Reversed, 0.1f, 0.0f);
        return RelativeDepthDelta(a, b);
    };
    CHECK(std::fabs(relDelta(1.0) - relDelta(1000.0)) < 2e-3);
    CHECK(relDelta(1.0) > 0.008 && relDelta(1.0) < 0.012);

    // ...whereas an ABSOLUTE threshold on raw reversed-Z depth is not: the same 1% step differs by ~1000x
    const double absNear = std::fabs(0.1 / 1.0 - 0.1 / 1.01);
    const double absFar  = std::fabs(0.1 / 1000.0 - 0.1 / 1010.0);
    CHECK(absNear / absFar > 500.0);

    // Sanitisation: Standard needs a finite far plane
    const DepthRange r1 = SanitizeDepthRange(DepthConvention::Standard, 0.1f, 0.0f);
    CHECK(r1.zFar == 1000.0f && r1.zNear == 0.1f);
    const DepthRange r2 = SanitizeDepthRange(DepthConvention::Reversed, 0.1f, 0.0f);
    CHECK(r2.zFar == 0.0f); // infinite far is valid for Reversed
    const DepthRange r3 = SanitizeDepthRange(DepthConvention::Standard, 5.0f, 2.0f);
    CHECK(r3.zFar == 1000.0f); // far <= near is invalid
}

static void TestPushConstantBuilder() {
    ConfigManager::Get().Modify([](CBRConfig& c) {
        c.depthTolerance = 0.02f;
        c.historyWeight = 0.8f;
        c.colorSpace = ColorSpace::RGB;
        c.enableSpatialFallback = false;
        c.enableMotionDilation = false;
        c.jitterCompensation = -1.0f;
        c.jitterScale = 1.0f;
        c.jitterDirection = -1;
        c.depthConvention = DepthConvention::Standard;
        c.depthNear = 0.5f;
        c.depthFar = 0.0f; // invalid for Standard -> must be sanitised
    });

    RenderTargetManager::Get().Initialize(3840, 2160);
    JitterManager::Get().Initialize(3840, 2160);
    JitterManager::Get().Update(0);
    JitterManager::Get().Update(1);

    const ReconstructionPushConstants pc = BuildReconstructionPushConstants(7);
    CHECK(pc.targetResolution[0] == 3840.0f && pc.targetResolution[1] == 2160.0f);
    CHECK(std::fabs(pc.invTargetResolution[0] - 1.0f / 3840.0f) < 1e-12f);
    CHECK(pc.frameIndex == 7);
    CHECK(pc.depthTolerance == 0.02f && pc.historyWeight == 0.8f);
    CHECK(pc.colorSpace == 1u);
    CHECK(pc.enableSpatialFallback == 0u);
    CHECK(pc.enableMotionDilation == 0u);
    CHECK(pc.jitterCompensation == -1.0f);
    CHECK(pc.shiftDirection == -1);
    CHECK(pc.depthMode == 0u);
    CHECK(pc.depthNear == 0.5f && pc.depthFar == 1000.0f);
    CHECK(std::fabs(pc.jitterDelta[0] - JitterManager::Get().GetJitterDelta().x) < 1e-12f);

    ConfigManager::Get().Modify([](CBRConfig& c) {
        c.enableSpatialFallback = true;
        c.enableMotionDilation = true;
        c.colorSpace = ColorSpace::YCoCg;
        c.jitterCompensation = 0.0f;
        c.jitterDirection = 1;
        c.depthConvention = DepthConvention::Reversed;
    });
    const ReconstructionPushConstants pc2 = BuildReconstructionPushConstants(0);
    CHECK(pc2.enableSpatialFallback == 1u && pc2.enableMotionDilation == 1u && pc2.colorSpace == 0u);
    CHECK(pc2.shiftDirection == 1 && pc2.depthMode == 1u && pc2.depthFar == 0.0f);
}

static void TestRenderTargets() {
    auto& r = RenderTargetManager::Get();
    // Invalid extents must be rejected without clobbering the last good state.
    r.Initialize(3840, 2160);
    const auto good = r.GetDimensions();
    r.Initialize(0, 0);
    CHECK(r.GetDimensions().fullWidth == good.fullWidth);
    r.Initialize(3840, 2160);
    // 4K: quarter colour 33,177,600 + quarter depth 16,588,800 + history colour A/B 132,710,400
    //    + history depth A/B 66,355,200 + output 66,355,200 = 315,187,200 bytes (300.58 MiB, 315.19 MB)
    CHECK(r.GetTotalAllocatedVramBytes() == 315187200u);
    const double mib = static_cast<double>(r.GetTotalAllocatedVramBytes()) / (1024.0 * 1024.0);
    CHECK(std::fabs(mib - 300.58) < 0.01);

    // 1080p (the AMD Vega 7 profile): 78,796,800 bytes = 78.80 MB
    r.Initialize(1920, 1080);
    CHECK(r.GetTotalAllocatedVramBytes() == 78796800u);
    r.Initialize(3840, 2160);

    CHECK(r.IsTargetInterceptCandidate(3840, 2160, 0));
    CHECK(!r.IsTargetInterceptCandidate(1920, 1080, 0));

    CHECK(r.GetCurrentHistoryIndex() == 0 && r.GetPreviousHistoryIndex() == 1);
    r.SwapHistoryBuffers();
    CHECK(r.GetCurrentHistoryIndex() == 1 && r.GetPreviousHistoryIndex() == 0);
    r.ResetHistory();
    CHECK(r.GetCurrentHistoryIndex() == 0);
}

static void TestPushConstantLayout() {
    CHECK(sizeof(ReconstructionPushConstants) == 80);
}

static void TestProjectionJitterSign() {
    auto& j = JitterManager::Get();
    j.Initialize(3840, 2160);
    j.Update(1); // odd frame has non-zero jitter in X

    const JitterOffset jitter = j.GetCurrentJitter();
    CHECK(jitter.x > 0.0f);

    // Default: projectionJitterSign = +1
    ConfigManager::Get().Modify([](CBRConfig& c) { c.projectionJitterSign = 1; });
    auto [ndcDx1, ndcDy1] = j.ComputeProjectionOffset(jitter, false);
    CHECK(ndcDx1 > 0.0f);
    CHECK(std::fabs(ndcDx1 - 2.0f * jitter.x) < 1e-7f);

    // Negated: projectionJitterSign = -1
    ConfigManager::Get().Modify([](CBRConfig& c) { c.projectionJitterSign = -1; });
    auto [ndcDx2, ndcDy2] = j.ComputeProjectionOffset(jitter, false);
    CHECK(ndcDx2 < 0.0f);
    CHECK(std::fabs(ndcDx2 + 2.0f * jitter.x) < 1e-7f);
    CHECK(std::fabs(ndcDx1 + ndcDx2) < 1e-7f);

    // Vulkan Y-flip behavior
    JitterOffset arbitraryJitter{ 0.001f, 0.002f };
    ConfigManager::Get().Modify([](CBRConfig& c) { c.projectionJitterSign = 1; });
    auto [vkX, vkY] = j.ComputeProjectionOffset(arbitraryJitter, true);
    auto [dxX, dxY] = j.ComputeProjectionOffset(arbitraryJitter, false);
    CHECK(vkX == dxX);
    CHECK(vkY == -dxY);

    // Reset default
    ConfigManager::Get().Modify([](CBRConfig& c) { c.projectionJitterSign = 1; });
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "cbr_tests";
    fs::create_directories(dir);

    TestConfigHardening(dir);
    TestConfigMissingFileAndRoundTrip(dir);
    TestLoggerBufferingAndLevel(dir);
    TestJitter();
    TestCheckerboardGeometry();
    TestDepthConvention();
    TestPushConstantBuilder();
    TestRenderTargets();
    TestPushConstantLayout();
    TestProjectionJitterSign();

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
