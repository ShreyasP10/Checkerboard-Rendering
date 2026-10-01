#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace {

HANDLE g_hInitThread = nullptr;

DWORD WINAPI CBRInitThread(LPVOID /*lpParam*/) {
    // Delay slightly to allow game engine core and graphics runtime to settle
    Sleep(1500);

    cbr::CBREngine::Get().Initialize();
    return 0;
}

std::string GetModuleDirectoryPath(HMODULE hModule) {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(hModule, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return "";
    }
    std::string fullPath(path);
    auto lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(0, lastSlash);
    }
    return "";
}

} // namespace

// Exported symbol ensuring the ASI plugin has an export table entry in PE header
extern "C" __declspec(dllexport) void CBR_PluginInit() {
    cbr::CBREngine::Get().Initialize();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            // Record module directory for resolving cbr.ini and cbr.log relative to the DLL
            cbr::CBREngine::Get().SetModuleDirectory(GetModuleDirectoryPath(hModule));

            // Launch initialization in background thread to avoid blocking process startup
            g_hInitThread = CreateThread(nullptr, 0, CBRInitThread, nullptr, 0, nullptr);
            break;
        }
        case DLL_PROCESS_DETACH: {
            bool isProcessExit = (lpReserved != nullptr);

            if (!isProcessExit && g_hInitThread) {
                // If dynamically unloaded via FreeLibrary, wait up to 2 seconds for init thread to terminate
                WaitForSingleObject(g_hInitThread, 2000);
            }

            if (g_hInitThread) {
                CloseHandle(g_hInitThread);
                g_hInitThread = nullptr;
            }

            cbr::CBREngine::Get().Shutdown(isProcessExit);
            break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

#endif
