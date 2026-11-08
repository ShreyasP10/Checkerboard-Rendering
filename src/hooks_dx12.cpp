#include "cbr/hooks.h"
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"
#include "cbr/render_target_manager.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

typedef long (__stdcall *PFN_D3D12Present)(void* swapChain, unsigned int syncInterval, unsigned int flags);
typedef long (__stdcall *PFN_D3D12Present1)(void* swapChain, unsigned int syncInterval, unsigned int flags, const void* pPresentParameters);
typedef long (__stdcall *PFN_D3D12ResizeBuffers)(void* swapChain, unsigned int bufferCount, unsigned int width, unsigned int height, unsigned int newFormat, unsigned int swapChainFlags);

PFN_D3D12Present       g_Original_D3D12Present = nullptr;
PFN_D3D12Present1      g_Original_D3D12Present1 = nullptr;
PFN_D3D12ResizeBuffers g_Original_D3D12ResizeBuffers = nullptr;
void*                  g_HookedPresentTarget = nullptr;
void*                  g_HookedPresent1Target = nullptr;
void*                  g_HookedResizeBuffersTarget = nullptr;

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

long __stdcall Hooked_D3D12Present1(void* swapChain, unsigned int syncInterval, unsigned int flags, const void* pPresentParameters) {
    if (!g_Original_D3D12Present1) {
        return kHResultFail;
    }

    try {
        CBREngine::Get().OnPrePresent(swapChain, nullptr);
    } catch (...) {
    }

    long result = g_Original_D3D12Present1(swapChain, syncInterval, flags, pPresentParameters);

    try {
        CBREngine::Get().OnPostPresent(swapChain);
    } catch (...) {
    }
    return result;
}

long __stdcall Hooked_D3D12ResizeBuffers(void* swapChain, unsigned int bufferCount, unsigned int width, unsigned int height, unsigned int newFormat, unsigned int swapChainFlags) {
    if (!g_Original_D3D12ResizeBuffers) {
        return kHResultFail;
    }

    CBR_LOG_INFO("DXGI SwapChain ResizeBuffers intercepted (%ux%u).", width, height);

    long result = g_Original_D3D12ResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags);

    if (result >= 0) { // S_OK
        try {
            CBREngine::Get().OnSwapchainRecreated(width, height);
        } catch (...) {
        }
    }
    return result;
}

#if defined(_WIN32)
struct MinimalDxgiRational {
    unsigned int Numerator;
    unsigned int Denominator;
};

struct MinimalDxgiModeDesc {
    unsigned int        Width;
    unsigned int        Height;
    MinimalDxgiRational RefreshRate;
    unsigned int        Format;
    unsigned int        ScanlineOrdering;
    unsigned int        Scaling;
};

struct MinimalDxgiSampleDesc {
    unsigned int Count;
    unsigned int Quality;
};

struct MinimalDxgiSwapChainDesc {
    MinimalDxgiModeDesc   BufferDesc;
    MinimalDxgiSampleDesc SampleDesc;
    unsigned int          BufferUsage;
    unsigned int          BufferCount;
    HWND                  OutputWindow;
    BOOL                  Windowed;
    unsigned int          SwapEffect;
    unsigned int          Flags;
};

struct MinimalD3D12CommandQueueDesc {
    unsigned int Type;
    int          Priority;
    unsigned int Flags;
    unsigned int NodeMask;
};

// Forward declaration of COM call helper
template <typename T, typename... Args>
inline HRESULT CallVmt(void* obj, size_t index, Args... args) {
    void** vmt = *reinterpret_cast<void***>(obj);
    auto func = reinterpret_cast<T>(vmt[index]);
    return func(obj, args...);
}

inline ULONG ReleaseCom(void* obj) {
    if (!obj) return 0;
    typedef ULONG (__stdcall *PFN_Release)(void*);
    void** vmt = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<PFN_Release>(vmt[2])(obj);
}
#endif

} // namespace

bool HookManager::InstallDX12Hooks() {
#if defined(_WIN32)
    HMODULE d3d12Module = GetModuleHandleA("d3d12.dll");
    HMODULE dxgiModule  = GetModuleHandleA("dxgi.dll");

    if (!d3d12Module || !dxgiModule) {
        static std::atomic<bool> s_warnedMissing{ false };
        if (!s_warnedMissing.exchange(true)) {
            CBR_LOG_WARN("d3d12.dll or dxgi.dll not yet loaded. Deferring DX12 hooks.");
        }
        return false;
    }

    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        CBR_LOG_ERROR("Failed to initialize MinHook core for DX12 (MH_STATUS: %d).", mhStatus);
        return false;
    }

    typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, int, REFIID, void**);
    typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID, void**);

    auto pfnD3D12CreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(GetProcAddress(d3d12Module, "D3D12CreateDevice"));
    auto pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(dxgiModule, "CreateDXGIFactory1"));

    if (!pfnD3D12CreateDevice || !pfnCreateDXGIFactory1) {
        CBR_LOG_WARN("Failed to resolve D3D12CreateDevice or CreateDXGIFactory1.");
        return false;
    }

    // Create a temporary hidden dummy window to create dummy swapchain
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "CBR_DX12DummyWindow";
    WNDCLASSA existingClass{};
    if (!GetClassInfoA(wc.hInstance, wc.lpszClassName, &existingClass)) {
        RegisterClassA(&wc);
    }

    HWND dummyHwnd = CreateWindowExA(0, wc.lpszClassName, "CBR_DX12Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummyHwnd) {
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    static const GUID kIID_ID3D12Device = 
        { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
    static const GUID kIID_ID3D12CommandQueue = 
        { 0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed } };
    static const GUID kIID_IDXGIFactory1 = 
        { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };

    void* d3d12Device = nullptr;
    HRESULT hr = pfnD3D12CreateDevice(nullptr, 0xb000 /* D3D_FEATURE_LEVEL_11_0 */, kIID_ID3D12Device, &d3d12Device);
    if (FAILED(hr) || !d3d12Device) {
        DestroyWindow(dummyHwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // VMT index 8 on ID3D12Device: CreateCommandQueue
    typedef HRESULT (__stdcall *PFN_CreateCommandQueue)(void*, const MinimalD3D12CommandQueueDesc*, REFIID, void**);
    MinimalD3D12CommandQueueDesc queueDesc{};
    queueDesc.Type = 0; // D3D12_COMMAND_LIST_TYPE_DIRECT
    void* cmdQueue = nullptr;
    hr = CallVmt<PFN_CreateCommandQueue>(d3d12Device, 8, &queueDesc, kIID_ID3D12CommandQueue, &cmdQueue);

    void* dxgiFactory = nullptr;
    hr = pfnCreateDXGIFactory1(kIID_IDXGIFactory1, &dxgiFactory);

    void* swapChain = nullptr;
    if (SUCCEEDED(hr) && dxgiFactory && cmdQueue) {
        MinimalDxgiSwapChainDesc scDesc{};
        scDesc.BufferDesc.Width = 100;
        scDesc.BufferDesc.Height = 100;
        scDesc.BufferDesc.Format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = 0x00000020; // DXGI_USAGE_RENDER_TARGET_OUTPUT
        scDesc.BufferCount = 2;
        scDesc.OutputWindow = dummyHwnd;
        scDesc.Windowed = TRUE;
        scDesc.SwapEffect = 4; // DXGI_SWAP_EFFECT_FLIP_DISCARD

        // VMT index 10 on IDXGIFactory1: CreateSwapChain
        typedef HRESULT (__stdcall *PFN_CreateSwapChain)(void*, void*, const MinimalDxgiSwapChainDesc*, void**);
        hr = CallVmt<PFN_CreateSwapChain>(dxgiFactory, 10, cmdQueue, &scDesc, &swapChain);
    }

    bool hooked = false;
    if (SUCCEEDED(hr) && swapChain) {
        void** swapChainVmt = *reinterpret_cast<void***>(swapChain);
        void* pfnPresent = swapChainVmt[8]; // IDXGISwapChain::Present
        void* pfnResize = swapChainVmt[13]; // IDXGISwapChain::ResizeBuffers

        if (pfnPresent) {
            mhStatus = MH_CreateHook(pfnPresent, reinterpret_cast<LPVOID>(&Hooked_D3D12Present), reinterpret_cast<LPVOID*>(&g_Original_D3D12Present));
            if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
                MH_EnableHook(pfnPresent);
                g_HookedPresentTarget = pfnPresent;
                hooked = true;
                CBR_LOG_INFO("MinHook: Successfully detoured D3D12/DXGI Present (VMT[8] at 0x%p).", pfnPresent);
            }
        }

        // VMT index 22 on IDXGISwapChain1: Present1
        void* pfnPresent1 = swapChainVmt[22];
        if (pfnPresent1) {
            mhStatus = MH_CreateHook(pfnPresent1, reinterpret_cast<LPVOID>(&Hooked_D3D12Present1), reinterpret_cast<LPVOID*>(&g_Original_D3D12Present1));
            if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
                MH_EnableHook(pfnPresent1);
                g_HookedPresent1Target = pfnPresent1;
                hooked = true;
                CBR_LOG_INFO("MinHook: Successfully detoured D3D12/DXGI Present1 (VMT[22] at 0x%p).", pfnPresent1);
            }
        }

        if (pfnResize) {
            mhStatus = MH_CreateHook(pfnResize, reinterpret_cast<LPVOID>(&Hooked_D3D12ResizeBuffers), reinterpret_cast<LPVOID*>(&g_Original_D3D12ResizeBuffers));
            if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
                MH_EnableHook(pfnResize);
                g_HookedResizeBuffersTarget = pfnResize;
                CBR_LOG_INFO("MinHook: Successfully detoured D3D12/DXGI ResizeBuffers (VMT[13] at 0x%p).", pfnResize);
            }
        }
    }

    // Clean up temporary dummy objects
    if (swapChain)   ReleaseCom(swapChain);
    if (dxgiFactory) ReleaseCom(dxgiFactory);
    if (cmdQueue)    ReleaseCom(cmdQueue);
    if (d3d12Device) ReleaseCom(d3d12Device);
    DestroyWindow(dummyHwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);

    if (hooked) {
        m_dx12Hooked.store(true);
        CBR_LOG_INFO("D3D12 graphics interception is now ACTIVE.");
        return true;
    }

    CBR_LOG_ERROR("Failed to establish D3D12 detour hooks via dummy swapchain.");
    return false;
#else
    return false;
#endif
}

void HookManager::UninstallDX12Hooks() {
    if (m_dx12Hooked.load()) {
        CBR_LOG_INFO("Restoring original DXGI/DX12 VMT hooks.");
        if (g_HookedPresentTarget) {
            MH_DisableHook(g_HookedPresentTarget);
            MH_RemoveHook(g_HookedPresentTarget);
            g_HookedPresentTarget = nullptr;
        }
        if (g_HookedPresent1Target) {
            MH_DisableHook(g_HookedPresent1Target);
            MH_RemoveHook(g_HookedPresent1Target);
            g_HookedPresent1Target = nullptr;
        }
        if (g_HookedResizeBuffersTarget) {
            MH_DisableHook(g_HookedResizeBuffersTarget);
            MH_RemoveHook(g_HookedResizeBuffersTarget);
            g_HookedResizeBuffersTarget = nullptr;
        }
        m_dx12Hooked.store(false);
    }
}

} // namespace cbr
