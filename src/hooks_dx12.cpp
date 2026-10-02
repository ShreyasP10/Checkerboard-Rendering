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

constexpr long kHResultFail = static_cast<long>(0x80004005u); // E_FAIL

long __stdcall Hooked_D3D12Present(void* swapChain, unsigned int syncInterval, unsigned int flags) {
    if (!g_Original_D3D12Present) {
        return kHResultFail;
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(swapChain, nullptr);
    } catch (...) {
    }

    long result = g_Original_D3D12Present(swapChain, syncInterval, flags);

    try {
        CBREngine::Get().OnPostPresent(swapChain);
    } catch (...) {
    }
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

    // TODO: locate IDXGISwapChain::Present via a dummy swapchain, detour it, and store the
    // trampoline in g_Original_D3D12Present. Until then NO hook is active.
    CBR_LOG_WARN("DX12 hook installation is not implemented yet; no hooks are active.");
    return false;
}

void HookManager::UninstallDX12Hooks() {
    if (m_dx12Hooked) {
        CBR_LOG_INFO("Restoring original DXGI/DX12 VMT hooks.");
        m_dx12Hooked = false;
    }
}

} // namespace cbr
