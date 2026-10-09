#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace {

DWORD WINAPI CBRInitThread(LPVOID /*lpParam*/) {
    // Config, logging and buffers do not depend on the graphics runtime, so set up immediately.
    auto& engine = cbr::CBREngine::Get();
    if (!engine.Initialize()) {
        return 0;
    }

    // Retry the hook installation itself (not just the trigger): the runtime for the configured
    // API may load well after this plugin. Poll every 250 ms for up to 60 s, then give up quietly.
    constexpr DWORD kIntervalMs = 250;
    constexpr DWORD kMaxAttempts = 240; // 240 * 250 ms = 60 seconds

    for (DWORD attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (engine.TryInstallHooks()) {
            CBR_LOG_INFO("Graphics hooks installed after %lu attempt(s).", static_cast<unsigned long>(attempt + 1));
            return 0;
        }
        Sleep(kIntervalMs);
    }

    CBR_LOG_WARN("Graphics hooks could not be installed within 60 s; CBR stays inactive.");
    return 0;
}

// Directory containing this module (wide-char API: safe for non-ASCII and long paths)
std::filesystem::path GetModuleDirectoryPath(HMODULE hModule) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(len);
            break;
        }
        if (buffer.size() >= 32768) { // longest possible NT path
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

} // namespace

// Exported symbol ensuring the ASI plugin has an export table entry in PE header
extern "C" __declspec(dllexport) void CBR_PluginInit() {
    cbr::CBREngine::Get().Initialize();
    cbr::CBREngine::Get().TryInstallHooks();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            // Pin this module so it can never be unmapped while the init thread or any
            // installed hook is still executing code inside it. ASI plugins are not meant
            // to be unloaded, and this removes the need to wait on a thread from DllMain.
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&CBR_PluginInit),
                &pinned)) {
                // Pin failed: do not spawn a thread that would execute unmapped code.
                // Record directory anyway so a later CBR_PluginInit can still initialize.
                cbr::CBREngine::Get().SetModuleDirectory(GetModuleDirectoryPath(hModule));
                break;
            }

            // Record module directory for resolving cbr.ini and cbr.log relative to the DLL
            cbr::CBREngine::Get().SetModuleDirectory(GetModuleDirectoryPath(hModule));

            // NOTE: CreateThread under the loader lock can deadlock. This is tolerated
            // only because the module is pinned and the thread touches just CBR singletons.
            // Preferred path is the loader calling CBR_PluginInit (no thread); this thread
            // is a fallback for loaders that only map the DLL.
            HANDLE hThread = CreateThread(nullptr, 0, CBRInitThread, nullptr, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            }
            break;
        }
        case DLL_PROCESS_DETACH:
            // Intentionally empty. Under the loader lock (and, on process exit, after other
            // threads have already been terminated) it is unsafe to take locks, join threads,
            // or tear down graphics hooks. The OS reclaims all resources at process exit.
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

#endif
