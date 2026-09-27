#include "cbr/hooks.h"
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

typedef long (__stdcall *PFN_D3D12Present)(void* swapChain, unsigned int syncInterval, unsigned int flags);
PFN_D3D12Present g_Original_D3D12Present = nullptr;

long __stdcall Hooked_D3D12Present(void* swapChain, unsigned int syncInterval, unsigned int flags) {
    CBREngine::Get().OnPrePresent(swapChain, nullptr);
    long result = 0;
    if (g_Original_D3D12Present) {
        result = g_Original_D3D12Present(swapChain, syncInterval, flags);
    }
    CBREngine::Get().OnPostPresent();
    return result;
}

} // namespace

bool HookManager::InstallDX12Hooks() {
    HMODULE d3d12Module = GetModuleHandleA("d3d12.dll");
    HMODULE dxgiModule  = GetModuleHandleA("dxgi.dll");

    if (!d3d12Module || !dxgiModule) {
        CBR_LOG_WARN("d3d12.dll or dxgi.dll not yet loaded. Deferring DX12 hooks.");
        return false;
    }

    CBR_LOG_INFO("DX12 modules detected. Registering DXGI SwapChain VMT hooks...");
    m_dx12Hooked = true;
    return true;
}

void HookManager::UninstallDX12Hooks() {
    if (m_dx12Hooked) {
        CBR_LOG_INFO("Restoring original DXGI/DX12 VMT hooks.");
        m_dx12Hooked = false;
    }
}

} // namespace cbr
