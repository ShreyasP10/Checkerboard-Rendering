// Host-side unit tests for the portable parts of the plugin (no Windows APIs, no GPU).
// Uses a custom CHECK macro (not assert) so tests still run in Release builds (NDEBUG).
#include "cbr/config.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace cbr;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                        \
    } while (0)

static void WriteFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p);
    f << text;
}

static std::string ReadFile(const fs::path& p) {
    std::ifstream f(p);
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
        "HistoryWeight = nan\n"       // NaN               -> default
        "DepthTolerance = 99999\n"    // out of range      -> clamped
        "MipLodBias = abc\n"          // garbage           -> default
        "[Jitter]\n"
        "JitterPattern = Halton\n"    // unimplemented     -> Checkerboard
        "JitterCompensation = -5\n"   // out of range      -> clamped to -1
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
    CHECK(c.debugView == 4);
    CHECK(!c.enableMotionDilation);
    CHECK(c.jitterCompensation == -1.0f);
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
    auto& cfg = ConfigManager::Get().GetMutableConfig();
    cfg.jitterScale = 1.0f;

    auto& j = JitterManager::Get();
    j.Initialize(3840, 2160);
    j.Update(0);
    const JitterOffset even = j.GetCurrentJitter();
    j.Update(1);
    const JitterOffset odd = j.GetCurrentJitter();

    CHECK(std::fabs(even.x + odd.x) < 1e-9f);                // alternating +/- phases
    CHECK(std::fabs(odd.x - 0.5f / 3840.0f) < 1e-9f);
    CHECK(std::fabs(odd.y) < 1e-9f);                         // 1D horizontal jitter (delta y = 0) per Intel CBR spec
    CHECK(std::fabs(even.y) < 1e-9f);
    CHECK(std::fabs(j.GetJitterDelta().x - (odd.x - even.x)) < 1e-9f);

    cfg.jitterScale = 2.0f;                                   // JitterScale must take effect
    j.Update(1);
    CHECK(std::fabs(j.GetCurrentJitter().x - 1.0f / 3840.0f) < 1e-9f);
    cfg.jitterScale = 1.0f;

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

static void TestPushConstantBuilder() {
    auto& cfg = ConfigManager::Get().GetMutableConfig();
    cfg.depthTolerance = 0.02f;
    cfg.historyWeight = 0.8f;
    cfg.colorSpace = ColorSpace::RGB;
    cfg.enableSpatialFallback = false;
    cfg.enableMotionDilation = false;
    cfg.jitterCompensation = -1.0f;
    cfg.jitterScale = 1.0f;

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
    CHECK(std::fabs(pc.jitterDelta[0] - JitterManager::Get().GetJitterDelta().x) < 1e-12f);

    cfg.enableSpatialFallback = true;
    cfg.enableMotionDilation = true;
    cfg.colorSpace = ColorSpace::YCoCg;
    cfg.jitterCompensation = 1.0f;
    const ReconstructionPushConstants pc2 = BuildReconstructionPushConstants(0);
    CHECK(pc2.enableSpatialFallback == 1u && pc2.enableMotionDilation == 1u && pc2.colorSpace == 0u);
}

static void TestRenderTargets() {
    auto& r = RenderTargetManager::Get();
    r.Initialize(3840, 2160);
    // 268.95 MB as documented in the README
    const double mb = static_cast<double>(r.GetTotalAllocatedVramBytes()) / (1024.0 * 1024.0);
    CHECK(std::fabs(mb - 268.95) < 0.01);
    CHECK(r.IsTargetInterceptCandidate(3840, 2160, 0));
    CHECK(!r.IsTargetInterceptCandidate(1920, 1080, 0));

    CHECK(r.GetCurrentHistoryIndex() == 0 && r.GetPreviousHistoryIndex() == 1);
    r.SwapHistoryBuffers();
    CHECK(r.GetCurrentHistoryIndex() == 1 && r.GetPreviousHistoryIndex() == 0);
    r.ResetHistory();
    CHECK(r.GetCurrentHistoryIndex() == 0);
}

static void TestPushConstantLayout() {
    CHECK(sizeof(ReconstructionPushConstants) == 64);
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "cbr_tests";
    fs::create_directories(dir);

    TestConfigHardening(dir);
    TestConfigMissingFileAndRoundTrip(dir);
    TestLoggerBufferingAndLevel(dir);
    TestJitter();
    TestPushConstantBuilder();
    TestRenderTargets();
    TestPushConstantLayout();

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
