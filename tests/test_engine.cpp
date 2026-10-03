// Engine-level tests: hook-install retry, once-per-frame dispatch, frame-parity gating.
// The real hook installers are Windows-only, so this file supplies fakes for them.
//
//   cbr_engine_tests            -> PreferredApi = Vulkan
//   cbr_engine_tests auto       -> PreferredApi = Auto (no graphics runtime present on the host)
//
// The engine is a process-wide singleton with a one-shot Initialize(), hence one mode per process.
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/hooks.h"
#include "cbr/render_target_manager.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

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

// ---- Fake hook installers (replace hooks_vulkan.cpp / hooks_dx12.cpp for host tests) ----
static std::atomic<int> g_vulkanAttempts{ 0 };
static std::atomic<int> g_dx12Attempts{ 0 };
static std::atomic<int> g_vulkanFailuresRemaining{ 0 };

namespace cbr {
bool HookManager::InstallVulkanHooks() {
    ++g_vulkanAttempts;
    if (g_vulkanFailuresRemaining.load() > 0) {
        --g_vulkanFailuresRemaining;
        return false;
    }
    m_vulkanHooked.store(true);
    return true;
}
bool HookManager::InstallDX12Hooks() { ++g_dx12Attempts; m_dx12Hooked.store(true); return true; }
void HookManager::UninstallVulkanHooks() { m_vulkanHooked.store(false); }
void HookManager::UninstallDX12Hooks()   { m_dx12Hooked.store(false); }
} // namespace cbr

static int g_a = 0, g_b = 0; // distinct addresses used as fake present targets

static void RunVulkanTests() {
    auto& e = CBREngine::Get();
    auto& rt = RenderTargetManager::Get();

    CHECK(e.Initialize());
    CHECK(e.GetActiveApi() == GraphicsApi::Vulkan);
    CHECK(g_vulkanAttempts.load() == 0);                  // Initialize() must not install hooks itself

    // Hook install is retried until it succeeds, then never repeated
    g_vulkanFailuresRemaining = 3;
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 4);
    CHECK(e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 4);                  // already hooked: no further attempts
    CHECK(g_dx12Attempts.load() == 0);

    // Reconstruction + history swap happen at most once per frame
    CHECK(e.GetCurrentFrameIndex() == 0);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 1);
    // One duplicate call: an unguarded engine would swap twice and land back on index 0. (An odd
    // number of duplicates would pass by parity luck, so keep the total number of calls even.)
    e.OnScenePassEnd(nullptr);                            // second matching pass in the same frame
    CHECK(rt.GetCurrentHistoryIndex() == 1);

    // Parity only advances for the main present target
    e.OnPostPresent(&g_a);                                // first present defines the main target
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_b);                                // overlay / secondary swapchain
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_a);
    CHECK(e.GetCurrentFrameIndex() == 2);

    // New frame -> one more swap allowed
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);

    // Disabled engine does nothing
    e.SetEnabled(false);
    e.OnPostPresent(&g_a);                                // frame advances, but...
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 0);              // ...no swap while disabled
    e.SetEnabled(true);

    // Swapchain recreation resets parity, history and the once-per-frame guard
    e.OnSwapchainRecreated();
    CHECK(e.GetCurrentFrameIndex() == 0);
    CHECK(rt.GetCurrentHistoryIndex() == 0);
    e.OnScenePassEnd(nullptr);
    CHECK(rt.GetCurrentHistoryIndex() == 1);              // frame 0 is dispatchable again
    e.OnPostPresent(&g_b);                                // a different target may become the main one
    CHECK(e.GetCurrentFrameIndex() == 1);
    e.OnPostPresent(&g_a);
    CHECK(e.GetCurrentFrameIndex() == 1);

    // Swapchain extents are untrusted: odd sizes round up to even, implausible ones are ignored
    e.OnSwapchainRecreated(1921, 1081);
    CHECK(rt.GetDimensions().fullWidth == 1922 && rt.GetDimensions().fullHeight == 1082);
    CHECK(rt.GetDimensions().quarterWidth == 961 && rt.GetDimensions().quarterHeight == 541);

    e.OnSwapchainRecreated(0, 0); // minimised window
    CHECK(rt.GetDimensions().fullWidth == 1922);

    e.OnSwapchainRecreated(5, 5); // too small
    CHECK(rt.GetDimensions().fullWidth == 1922);

    e.OnSwapchainRecreated(100000, 100000); // absurd
    CHECK(rt.GetDimensions().fullWidth == 1922 && rt.GetDimensions().fullHeight == 1082);

    e.OnSwapchainRecreated(3840, 2160);
    CHECK(rt.GetDimensions().fullWidth == 3840 && rt.GetDimensions().fullHeight == 2160);
}

static void RunAutoTests() {
    auto& e = CBREngine::Get();
    CHECK(e.Initialize());
    // No graphics runtime is loaded on the host: Auto must stay pending and attempt NO installs
    CHECK(!e.TryInstallHooks());
    CHECK(!e.TryInstallHooks());
    CHECK(g_vulkanAttempts.load() == 0);
    CHECK(g_dx12Attempts.load() == 0);
}

int main(int argc, char** argv) {
    const bool autoMode = (argc > 1 && std::strcmp(argv[1], "auto") == 0);

    const fs::path dir = fs::temp_directory_path() / (autoMode ? "cbr_engine_tests_auto" : "cbr_engine_tests");
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "cbr.ini");
        f << "[General]\nEnabled = true\nTargetWidth = 3840\nTargetHeight = 2160\n"
          << "PreferredApi = " << (autoMode ? "Auto" : "Vulkan") << "\n[Debug]\nLogToFile = false\n";
    }
    CBREngine::Get().SetModuleDirectory(dir);

    if (autoMode) RunAutoTests(); else RunVulkanTests();

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << (autoMode ? "Engine (auto) tests passed.\n" : "Engine tests passed.\n");
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
