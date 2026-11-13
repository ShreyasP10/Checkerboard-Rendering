#include "cbr/hooks.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
#include <MinHook.h>
#include "cbr/cbr_engine.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/pattern_scanner.h"
#include "cbr/reconstruction_pass.h"
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
typedef void (__stdcall *PFN_D3D12ExecuteCommandLists)(void* commandQueue, unsigned int numCommandLists, void* const* ppCommandLists);
typedef void (__stdcall *PFN_D3D12RSSetViewports)(void* commandList, unsigned int numViewports, const void* pViewports);
typedef void (__stdcall *PFN_D3D12RSSetScissorRects)(void* commandList, unsigned int numRects, const void* pRects);
typedef void (__stdcall *PFN_D3D12OMSetRenderTargets)(void* commandList, unsigned int numRTDescriptors, const void* pRTDescriptors, int rtsSingleHandleToRange, const void* pDepthDescriptor);
typedef void (__stdcall *PFN_D3D12DrawInstanced)(void* commandList, unsigned int vertexCount, unsigned int instanceCount, unsigned int startVertex, unsigned int startInstance);
typedef void (__stdcall *PFN_D3D12DrawIndexedInstanced)(void* commandList, unsigned int indexCount, unsigned int instanceCount, unsigned int startIndex, int baseVertex, unsigned int startInstance);
typedef void (__stdcall *PFN_D3D12SetPipelineState)(void* commandList, void* pPipelineState);
typedef void (__stdcall *PFN_D3D12SetDescriptorHeaps)(void* commandList, unsigned int numDescriptorHeaps, void* const* ppDescriptorHeaps);
typedef void (__stdcall *PFN_D3D12SetComputeRootSignature)(void* commandList, void* pRootSignature);
typedef void (__stdcall *PFN_D3D12SetGraphicsRootSignature)(void* commandList, void* pRootSignature);

PFN_D3D12Present              g_Original_D3D12Present = nullptr;
PFN_D3D12Present1             g_Original_D3D12Present1 = nullptr;
PFN_D3D12ResizeBuffers        g_Original_D3D12ResizeBuffers = nullptr;
PFN_D3D12ExecuteCommandLists  g_Original_D3D12ExecuteCommandLists = nullptr;
PFN_D3D12RSSetViewports       g_Original_D3D12RSSetViewports = nullptr;
PFN_D3D12RSSetScissorRects    g_Original_D3D12RSSetScissorRects = nullptr;
PFN_D3D12OMSetRenderTargets   g_Original_D3D12OMSetRenderTargets = nullptr;
PFN_D3D12DrawInstanced        g_Original_D3D12DrawInstanced = nullptr;
PFN_D3D12DrawIndexedInstanced g_Original_D3D12DrawIndexedInstanced = nullptr;
PFN_D3D12SetPipelineState    g_Original_D3D12SetPipelineState = nullptr;
PFN_D3D12SetDescriptorHeaps  g_Original_D3D12SetDescriptorHeaps = nullptr;
PFN_D3D12SetComputeRootSignature g_Original_D3D12SetComputeRootSignature = nullptr;
PFN_D3D12SetGraphicsRootSignature g_Original_D3D12SetGraphicsRootSignature = nullptr;

std::mutex         g_dx12HookMutex;
std::vector<void*> g_HookedDX12Targets;
std::atomic<void*> g_liveD3D12Device{ nullptr };

constexpr long kHResultFail = static_cast<long>(0x80004005u); // E_FAIL

#if defined(_WIN32)
struct MinimalD3D12Viewport {
    float TopLeftX;
    float TopLeftY;
    float Width;
    float Height;
    float MinDepth;
    float MaxDepth;
};
static_assert(sizeof(MinimalD3D12Viewport) == 24, "D3D12_VIEWPORT size mismatch");

struct MinimalD3D12Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(MinimalD3D12Rect) == 16, "D3D12_RECT size mismatch");

struct DX12CmdListSlot {
    std::atomic<void*>    cmdList{ nullptr };
    std::atomic<bool>     viewportDownscaled{ false };
    std::atomic<uint32_t> drawCount{ 0 };
    std::atomic<void*>    lastPipelineState{ nullptr };
    std::atomic<void*>    lastGraphicsRootSig{ nullptr };
    std::atomic<void*>    lastComputeRootSig{ nullptr };
    std::mutex            heapMutex;
    uint32_t              lastNumDescriptorHeaps{ 0 };
    void*                 lastDescriptorHeaps[8]{ nullptr };
};

constexpr size_t kMaxDX12Slots = 64;
static DX12CmdListSlot g_dx12Slots[kMaxDX12Slots];

inline DX12CmdListSlot* GetOrCreateDX12Slot(void* cmdList) {
    if (!cmdList) return nullptr;
    const size_t start = (reinterpret_cast<uintptr_t>(cmdList) >> 4) % kMaxDX12Slots;
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (start + i) % kMaxDX12Slots;
        if (g_dx12Slots[idx].cmdList.load(std::memory_order_relaxed) == cmdList) {
            return &g_dx12Slots[idx];
        }
    }
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (start + i) % kMaxDX12Slots;
        void* expected = nullptr;
        if (g_dx12Slots[idx].cmdList.compare_exchange_strong(expected, cmdList, std::memory_order_acq_rel)) {
            g_dx12Slots[idx].viewportDownscaled.store(false, std::memory_order_relaxed);
            g_dx12Slots[idx].drawCount.store(0, std::memory_order_relaxed);
            g_dx12Slots[idx].lastPipelineState.store(nullptr, std::memory_order_relaxed);
            g_dx12Slots[idx].lastGraphicsRootSig.store(nullptr, std::memory_order_relaxed);
            g_dx12Slots[idx].lastComputeRootSig.store(nullptr, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(g_dx12Slots[idx].heapMutex);
                g_dx12Slots[idx].lastNumDescriptorHeaps = 0;
                for (size_t j = 0; j < 8; ++j) g_dx12Slots[idx].lastDescriptorHeaps[j] = nullptr;
            }
            return &g_dx12Slots[idx];
        }
    }
    return nullptr;
}

inline DX12CmdListSlot* FindDX12Slot(void* cmdList) {
    if (!cmdList) return nullptr;
    const size_t start = (reinterpret_cast<uintptr_t>(cmdList) >> 4) % kMaxDX12Slots;
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (start + i) % kMaxDX12Slots;
        if (g_dx12Slots[idx].cmdList.load(std::memory_order_relaxed) == cmdList) {
            return &g_dx12Slots[idx];
        }
    }
    return nullptr;
}

inline void ReleaseDX12Slot(void* cmdList) {
    DX12CmdListSlot* slot = FindDX12Slot(cmdList);
    if (slot) {
        slot->viewportDownscaled.store(false, std::memory_order_relaxed);
        slot->drawCount.store(0, std::memory_order_relaxed);
        slot->lastPipelineState.store(nullptr, std::memory_order_relaxed);
        slot->lastGraphicsRootSig.store(nullptr, std::memory_order_relaxed);
        slot->lastComputeRootSig.store(nullptr, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(slot->heapMutex);
            slot->lastNumDescriptorHeaps = 0;
            for (size_t i = 0; i < 8; ++i) slot->lastDescriptorHeaps[i] = nullptr;
        }
        slot->cmdList.store(nullptr, std::memory_order_release);
    }
}
#endif

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

// Type-safe and null-checked COM VMT invocation helper
template <typename T, typename... Args>
inline HRESULT CallVmt(void* obj, size_t index, Args... args) {
    if (!obj) return E_POINTER;
    void** vmt = *reinterpret_cast<void***>(obj);
    if (!vmt) return E_POINTER;
    auto func = reinterpret_cast<T>(vmt[index]);
    if (!func) return E_UNEXPECTED;
    return func(obj, args...);
}

inline ULONG ReleaseCom(void* obj) {
    if (!obj) return 0;
    typedef ULONG (__stdcall *PFN_Release)(void*);
    void** vmt = *reinterpret_cast<void***>(obj);
    if (!vmt || !vmt[2]) return 0;
    return reinterpret_cast<PFN_Release>(vmt[2])(obj);
}

void __stdcall Hooked_D3D12ExecuteCommandLists(void* commandQueue, unsigned int numCommandLists, void* const* ppCommandLists) {
    if (commandQueue && CBREngine::Get().IsEnabled()) {
        try {
            // Capture the live ID3D12Device from the game's command queue on first execution (Item H-02)
            static const GUID kIID_ID3D12Device =
                { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
            void* liveDev = nullptr;
            typedef HRESULT (__stdcall *PFN_GetDevice)(void*, const GUID*, void**);
            if (SUCCEEDED(CallVmt<PFN_GetDevice>(commandQueue, 7, &kIID_ID3D12Device, &liveDev)) && liveDev) {
                void* prevDev = g_liveD3D12Device.load(std::memory_order_acquire);
                if (prevDev != liveDev || !RenderTargetManager::Get().AreDX12ResourcesCreated()) {
                    if (g_liveD3D12Device.exchange(liveDev, std::memory_order_acq_rel) != liveDev ||
                        !RenderTargetManager::Get().AreDX12ResourcesCreated()) {
                        if (RenderTargetManager::Get().CreateDX12Resources(liveDev)) {
                            ReconstructionPass::Get().InitializeDX12(liveDev);
                        }
                    }
                }
                ReleaseCom(liveDev);
            }

            if (ppCommandLists && numCommandLists > 0) {
                for (unsigned int i = 0; i < numCommandLists; ++i) {
                    ReleaseDX12Slot(ppCommandLists[i]);
                }
            }
        } catch (...) {
        }
    }

    if (g_Original_D3D12ExecuteCommandLists) {
        g_Original_D3D12ExecuteCommandLists(commandQueue, numCommandLists, ppCommandLists);
    }
}

void __stdcall Hooked_D3D12RSSetViewports(void* commandList, unsigned int numViewports, const void* pViewports) {
    if (!g_Original_D3D12RSSetViewports) return;

    if (!pViewports || numViewports == 0 || !CBREngine::Get().IsEnabled()) {
        g_Original_D3D12RSSetViewports(commandList, numViewports, pViewports);
        return;
    }

    MinimalD3D12Viewport firstVp{};
    std::memcpy(&firstVp, pViewports, sizeof(firstVp));
    const auto dims = RenderTargetManager::Get().GetDimensions();
    if (numViewports == 1 && RenderTargetManager::Get().IsTargetInterceptCandidate(
            static_cast<uint32_t>(firstVp.Width),
            static_cast<uint32_t>(firstVp.Height),
            0)) {
        const auto config = CBREngine::Get().GetCachedFrameConfig();
        const uint32_t currentFrame = CBREngine::Get().GetCurrentFrameIndex();
        if (config.samplingStrategy == SamplingStrategy::StrategyA && config.jitterDirection != 0) {
            PatternScanner::UpdateCameraJitter(currentFrame, false);
        }

        DX12CmdListSlot* slot = GetOrCreateDX12Slot(commandList);
        if (slot) {
            slot->viewportDownscaled.store(true, std::memory_order_release);
        }

        firstVp.Width = static_cast<float>(dims.quarterWidth);
        firstVp.Height = static_cast<float>(dims.quarterHeight);
        if (config.samplingStrategy == SamplingStrategy::StrategyA &&
            config.jitterDirection != 0 &&
            PatternScanner::GetCameraProjectionMatrix() == nullptr) {
            firstVp.TopLeftX += JitterManager::Get().GetQuarterViewportShiftX(currentFrame);
        }

        g_Original_D3D12RSSetViewports(commandList, 1, &firstVp);
        return;
    }

    g_Original_D3D12RSSetViewports(commandList, numViewports, pViewports);
}

void __stdcall Hooked_D3D12RSSetScissorRects(void* commandList, unsigned int numRects, const void* pRects) {
    if (!g_Original_D3D12RSSetScissorRects) return;

    if (!pRects || numRects == 0 || !CBREngine::Get().IsEnabled()) {
        g_Original_D3D12RSSetScissorRects(commandList, numRects, pRects);
        return;
    }

    MinimalD3D12Rect firstRect{};
    std::memcpy(&firstRect, pRects, sizeof(firstRect));
    const uint32_t width = (firstRect.right > firstRect.left) ? static_cast<uint32_t>(firstRect.right - firstRect.left) : 0u;
    const uint32_t height = (firstRect.bottom > firstRect.top) ? static_cast<uint32_t>(firstRect.bottom - firstRect.top) : 0u;
    const auto dims = RenderTargetManager::Get().GetDimensions();
    if (numRects == 1 && RenderTargetManager::Get().IsTargetInterceptCandidate(width, height, 0)) {
        firstRect.right = firstRect.left + static_cast<int32_t>(dims.quarterWidth);
        firstRect.bottom = firstRect.top + static_cast<int32_t>(dims.quarterHeight);
        g_Original_D3D12RSSetScissorRects(commandList, 1, &firstRect);
        return;
    }

    g_Original_D3D12RSSetScissorRects(commandList, numRects, pRects);
}

void __stdcall Hooked_D3D12SetPipelineState(void* commandList, void* pPipelineState) {
    if (CBREngine::Get().IsEnabled() && commandList) {
        DX12CmdListSlot* slot = GetOrCreateDX12Slot(commandList);
        if (slot) {
            slot->lastPipelineState.store(pPipelineState, std::memory_order_release);
        }
    }
    if (g_Original_D3D12SetPipelineState) {
        g_Original_D3D12SetPipelineState(commandList, pPipelineState);
    }
}

void __stdcall Hooked_D3D12SetDescriptorHeaps(void* commandList, unsigned int numDescriptorHeaps, void* const* ppDescriptorHeaps) {
    if (CBREngine::Get().IsEnabled() && commandList) {
        DX12CmdListSlot* slot = GetOrCreateDX12Slot(commandList);
        if (slot) {
            std::lock_guard<std::mutex> lock(slot->heapMutex);
            const uint32_t count = (numDescriptorHeaps > 8u) ? 8u : numDescriptorHeaps;
            slot->lastNumDescriptorHeaps = count;
            for (uint32_t i = 0; i < count; ++i) {
                slot->lastDescriptorHeaps[i] = ppDescriptorHeaps ? ppDescriptorHeaps[i] : nullptr;
            }
        }
    }
    if (g_Original_D3D12SetDescriptorHeaps) {
        g_Original_D3D12SetDescriptorHeaps(commandList, numDescriptorHeaps, ppDescriptorHeaps);
    }
}

void __stdcall Hooked_D3D12SetComputeRootSignature(void* commandList, void* pRootSignature) {
    if (CBREngine::Get().IsEnabled() && commandList) {
        DX12CmdListSlot* slot = GetOrCreateDX12Slot(commandList);
        if (slot) {
            slot->lastComputeRootSig.store(pRootSignature, std::memory_order_release);
        }
    }
    if (g_Original_D3D12SetComputeRootSignature) {
        g_Original_D3D12SetComputeRootSignature(commandList, pRootSignature);
    }
}

void __stdcall Hooked_D3D12SetGraphicsRootSignature(void* commandList, void* pRootSignature) {
    if (CBREngine::Get().IsEnabled() && commandList) {
        DX12CmdListSlot* slot = GetOrCreateDX12Slot(commandList);
        if (slot) {
            slot->lastGraphicsRootSig.store(pRootSignature, std::memory_order_release);
        }
    }
    if (g_Original_D3D12SetGraphicsRootSignature) {
        g_Original_D3D12SetGraphicsRootSignature(commandList, pRootSignature);
    }
}

void __stdcall Hooked_D3D12OMSetRenderTargets(void* commandList, unsigned int numRTDescriptors, const void* pRTDescriptors, int rtsSingleHandleToRange, const void* pDepthDescriptor) {
    if (CBREngine::Get().IsEnabled() && commandList) {
        try {
            // If an intercepted scene pass just finished drawing on this open command list and is now switching targets,
            // inject CBR compute reconstruction before downstream post-processing / UI targets bind (Item H-02).
            DX12CmdListSlot* slot = FindDX12Slot(commandList);
            if (slot && slot->viewportDownscaled.load(std::memory_order_acquire) &&
                slot->drawCount.load(std::memory_order_acquire) >= 250u) {
                
                // Dispatch CBR compute reconstruction
                CBREngine::Get().OnScenePassEnd(commandList);

                // Restore game's active graphics/compute pipeline state and descriptor heaps (Item C-03)
                if (g_Original_D3D12SetDescriptorHeaps) {
                    std::lock_guard<std::mutex> lock(slot->heapMutex);
                    if (slot->lastNumDescriptorHeaps > 0) {
                        g_Original_D3D12SetDescriptorHeaps(commandList, slot->lastNumDescriptorHeaps, slot->lastDescriptorHeaps);
                    }
                }
                void* pso = slot->lastPipelineState.load(std::memory_order_acquire);
                if (pso && g_Original_D3D12SetPipelineState) {
                    g_Original_D3D12SetPipelineState(commandList, pso);
                }
                void* gfxSig = slot->lastGraphicsRootSig.load(std::memory_order_acquire);
                if (gfxSig && g_Original_D3D12SetGraphicsRootSignature) {
                    g_Original_D3D12SetGraphicsRootSignature(commandList, gfxSig);
                }
                void* compSig = slot->lastComputeRootSig.load(std::memory_order_acquire);
                if (compSig && g_Original_D3D12SetComputeRootSignature) {
                    g_Original_D3D12SetComputeRootSignature(commandList, compSig);
                }

                ReleaseDX12Slot(commandList);
            }
        } catch (...) {
        }
    }

    if (g_Original_D3D12OMSetRenderTargets) {
        g_Original_D3D12OMSetRenderTargets(commandList, numRTDescriptors, pRTDescriptors, rtsSingleHandleToRange, pDepthDescriptor);
    }
}

void __stdcall Hooked_D3D12DrawInstanced(void* commandList, unsigned int vertexCount, unsigned int instanceCount, unsigned int startVertex, unsigned int startInstance) {
    if (CBREngine::Get().IsEnabled()) {
        DX12CmdListSlot* slot = FindDX12Slot(commandList);
        if (slot) {
            slot->drawCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (g_Original_D3D12DrawInstanced) {
        g_Original_D3D12DrawInstanced(commandList, vertexCount, instanceCount, startVertex, startInstance);
    }
}

void __stdcall Hooked_D3D12DrawIndexedInstanced(void* commandList, unsigned int indexCount, unsigned int instanceCount, unsigned int startIndex, int baseVertex, unsigned int startInstance) {
    if (CBREngine::Get().IsEnabled()) {
        DX12CmdListSlot* slot = FindDX12Slot(commandList);
        if (slot) {
            slot->drawCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (g_Original_D3D12DrawIndexedInstanced) {
        g_Original_D3D12DrawIndexedInstanced(commandList, indexCount, instanceCount, startIndex, baseVertex, startInstance);
    }
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

    MH_STATUS mhStatus = MH_OK;

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
    static const GUID kIID_ID3D12CommandAllocator =
        { 0x6102dee4, 0xaf59, 0x4b09, { 0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24 } };
    static const GUID kIID_ID3D12GraphicsCommandList =
        { 0x5b160d0f, 0xac1b, 0x4185, { 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55 } };
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

    // Create temporary allocator & graphics command list for VMT hooking (Item H-02)
    typedef HRESULT (__stdcall *PFN_CreateCommandAllocator)(void*, unsigned int, REFIID, void**);
    typedef HRESULT (__stdcall *PFN_CreateCommandList)(void*, unsigned int, unsigned int, void*, void*, REFIID, void**);
    void* cmdAllocator = nullptr;
    void* cmdList = nullptr;
    if (SUCCEEDED(CallVmt<PFN_CreateCommandAllocator>(d3d12Device, 9, 0u, kIID_ID3D12CommandAllocator, &cmdAllocator)) && cmdAllocator) {
        if (SUCCEEDED(CallVmt<PFN_CreateCommandList>(d3d12Device, 12, 0u, 0u, cmdAllocator, nullptr, kIID_ID3D12GraphicsCommandList, &cmdList)) && cmdList) {
            typedef HRESULT (__stdcall *PFN_CloseCmdList)(void*);
            CallVmt<PFN_CloseCmdList>(cmdList, 9);
        }
    }

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

    auto installVmtHook = [&](void* target, void* detour, void** original, const char* name) -> bool {
        if (!target) return false;
        mhStatus = MH_CreateHook(target, detour, original);
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(target);
            std::lock_guard<std::mutex> lock(g_dx12HookMutex);
            g_HookedDX12Targets.push_back(target);
            CBR_LOG_INFO("MinHook: Successfully detoured %s (at 0x%p).", name, target);
            return true;
        }
        return false;
    };

    bool hooked = false;
    if (SUCCEEDED(hr) && swapChain) {
        void** swapChainVmt = *reinterpret_cast<void***>(swapChain);
        hooked |= installVmtHook(swapChainVmt[8],  reinterpret_cast<LPVOID>(&Hooked_D3D12Present),       reinterpret_cast<LPVOID*>(&g_Original_D3D12Present),       "IDXGISwapChain::Present[8]");
        hooked |= installVmtHook(swapChainVmt[22], reinterpret_cast<LPVOID>(&Hooked_D3D12Present1),      reinterpret_cast<LPVOID*>(&g_Original_D3D12Present1),      "IDXGISwapChain1::Present1[22]");
        installVmtHook(swapChainVmt[13],           reinterpret_cast<LPVOID>(&Hooked_D3D12ResizeBuffers), reinterpret_cast<LPVOID*>(&g_Original_D3D12ResizeBuffers), "IDXGISwapChain::ResizeBuffers[13]");
    }

    if (cmdQueue) {
        void** queueVmt = *reinterpret_cast<void***>(cmdQueue);
        if (queueVmt) {
            installVmtHook(queueVmt[10], reinterpret_cast<LPVOID>(&Hooked_D3D12ExecuteCommandLists), reinterpret_cast<LPVOID*>(&g_Original_D3D12ExecuteCommandLists), "ID3D12CommandQueue::ExecuteCommandLists[10]");
        }
    }

    if (cmdList) {
        void** listVmt = *reinterpret_cast<void***>(cmdList);
        if (listVmt) {
            installVmtHook(listVmt[12], reinterpret_cast<LPVOID>(&Hooked_D3D12DrawInstanced),        reinterpret_cast<LPVOID*>(&g_Original_D3D12DrawInstanced),        "ID3D12GraphicsCommandList::DrawInstanced[12]");
            installVmtHook(listVmt[13], reinterpret_cast<LPVOID>(&Hooked_D3D12DrawIndexedInstanced), reinterpret_cast<LPVOID*>(&g_Original_D3D12DrawIndexedInstanced), "ID3D12GraphicsCommandList::DrawIndexedInstanced[13]");
            installVmtHook(listVmt[21], reinterpret_cast<LPVOID>(&Hooked_D3D12RSSetViewports),       reinterpret_cast<LPVOID*>(&g_Original_D3D12RSSetViewports),       "ID3D12GraphicsCommandList::RSSetViewports[21]");
            installVmtHook(listVmt[22], reinterpret_cast<LPVOID>(&Hooked_D3D12RSSetScissorRects),    reinterpret_cast<LPVOID*>(&g_Original_D3D12RSSetScissorRects),    "ID3D12GraphicsCommandList::RSSetScissorRects[22]");
            installVmtHook(listVmt[25], reinterpret_cast<LPVOID>(&Hooked_D3D12SetPipelineState),    reinterpret_cast<LPVOID*>(&g_Original_D3D12SetPipelineState),    "ID3D12GraphicsCommandList::SetPipelineState[25]");
            installVmtHook(listVmt[28], reinterpret_cast<LPVOID>(&Hooked_D3D12SetDescriptorHeaps),  reinterpret_cast<LPVOID*>(&g_Original_D3D12SetDescriptorHeaps),  "ID3D12GraphicsCommandList::SetDescriptorHeaps[28]");
            installVmtHook(listVmt[29], reinterpret_cast<LPVOID>(&Hooked_D3D12SetComputeRootSignature), reinterpret_cast<LPVOID*>(&g_Original_D3D12SetComputeRootSignature), "ID3D12GraphicsCommandList::SetComputeRootSignature[29]");
            installVmtHook(listVmt[30], reinterpret_cast<LPVOID>(&Hooked_D3D12SetGraphicsRootSignature), reinterpret_cast<LPVOID*>(&g_Original_D3D12SetGraphicsRootSignature), "ID3D12GraphicsCommandList::SetGraphicsRootSignature[30]");
            installVmtHook(listVmt[46], reinterpret_cast<LPVOID>(&Hooked_D3D12OMSetRenderTargets),   reinterpret_cast<LPVOID*>(&g_Original_D3D12OMSetRenderTargets),   "ID3D12GraphicsCommandList::OMSetRenderTargets[46]");
        }
    }

    // Clean up temporary dummy objects
    if (cmdList)      ReleaseCom(cmdList);
    if (cmdAllocator) ReleaseCom(cmdAllocator);
    if (swapChain)    ReleaseCom(swapChain);
    if (dxgiFactory)  ReleaseCom(dxgiFactory);
    if (cmdQueue)     ReleaseCom(cmdQueue);
    if (d3d12Device)  ReleaseCom(d3d12Device);
    if (dummyHwnd) {
        if (!DestroyWindow(dummyHwnd)) {
            CBR_LOG_WARN("HooksDX12: DestroyWindow on dummy window returned error: %lu (Item 92)", GetLastError());
        }
        dummyHwnd = nullptr;
    }
    if (!UnregisterClassA(wc.lpszClassName, wc.hInstance)) {
        CBR_LOG_WARN("HooksDX12: UnregisterClassA returned error: %lu", GetLastError());
    }

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
        std::lock_guard<std::mutex> lock(g_dx12HookMutex);
        for (void* target : g_HookedDX12Targets) {
            if (target) {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
        }
        g_HookedDX12Targets.clear();
        m_dx12Hooked.store(false);
        g_Original_D3D12SetPipelineState = nullptr;
        g_Original_D3D12SetDescriptorHeaps = nullptr;
        g_Original_D3D12SetComputeRootSignature = nullptr;
        g_Original_D3D12SetGraphicsRootSignature = nullptr;
    }
}

} // namespace cbr
