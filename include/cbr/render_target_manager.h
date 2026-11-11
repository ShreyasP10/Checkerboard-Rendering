#pragma once

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <vector>

namespace cbr {

struct TargetDimensions {
    uint32_t fullWidth{ 3840 };
    uint32_t fullHeight{ 2160 };
    uint32_t quarterWidth{ 1920 };
    uint32_t quarterHeight{ 1080 };
    uint32_t msaaSamples{ 2 };
    float    invFullWidth{ 1.0f / 3840.0f };
    float    invFullHeight{ 1.0f / 2160.0f };
};

struct VulkanTextureResource {
    void*    image{ nullptr };
    void*    memory{ nullptr };
    void*    view{ nullptr };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t format{ 0 };
    uint32_t aspectMask{ 1 };     // VK_IMAGE_ASPECT_COLOR_BIT
    uint32_t currentLayout{ 0 };  // Tracked VkImageLayout (0 = VK_IMAGE_LAYOUT_UNDEFINED)
};

struct VulkanImageMetadata {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t format{ 0 };
    uint32_t samples{ 0 }; // 0 = unknown (pre-hook image), 1 = 1x, 2 = 2x MSAA, etc.
    uint32_t usage{ 0 };
};

struct VulkanRenderTargetSet {
    VulkanTextureResource quarterColor;
    VulkanTextureResource quarterDepth;
    VulkanTextureResource velocity;
    VulkanTextureResource historyColor[2];
    VulkanTextureResource historyDepth[2];
    VulkanTextureResource outputColor;
    VulkanTextureResource outputDepth;
    void*                 linearSampler{ nullptr };
    void*                 pointSampler{ nullptr };
};

struct DX12TextureResource {
    void*    resource{ nullptr };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t format{ 0 };
    uint32_t samples{ 1 };
    uint32_t currentState{ 0 }; // D3D12_RESOURCE_STATE_COMMON = 0
};

struct DX12RenderTargetSet {
    DX12TextureResource quarterColor;
    DX12TextureResource quarterDepth;
    DX12TextureResource velocity;
    DX12TextureResource historyColor[2];
    DX12TextureResource historyDepth[2];
    DX12TextureResource outputColor;
    DX12TextureResource outputDepth;
    void*               srvUavHeap{ nullptr };
    void*               samplerHeap{ nullptr };
    uint64_t            srvGpuHandle[2]{ 0, 0 };
    uint64_t            uavGpuHandle[2]{ 0, 0 };
    uint64_t            samplerGpuHandle{ 0 };
};

class RenderTargetManager {
public:
    static RenderTargetManager& Get();

    void Initialize(uint32_t width, uint32_t height);
    void Shutdown();

    // Vulkan GPU resource management (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    bool CreateVulkanResources(void* vkDevice, void* vkPhysicalDevice);
    void DestroyVulkanResources();
    [[nodiscard]] const VulkanRenderTargetSet& GetVulkanResources() const { return m_vkResources; }
    [[nodiscard]] bool AreVulkanResourcesCreated() const { return m_vkResourcesCreated.load(); }
    void SetVulkanImageLayout(void* vkImage, uint32_t newLayout);
    [[nodiscard]] uint32_t GetVulkanImageLayout(void* vkImage) const;

    // DirectX 12 GPU resource & descriptor heap management (Item H-02)
    bool CreateDX12Resources(void* d3d12Device);
    void DestroyDX12Resources();
    [[nodiscard]] const DX12RenderTargetSet& GetDX12Resources() const { return m_dx12Resources; }
    [[nodiscard]] bool AreDX12ResourcesCreated() const { return m_dx12ResourcesCreated.load(); }
    void SetDX12ResourceState(void* d3d12Resource, uint32_t newState);
    [[nodiscard]] uint32_t GetDX12ResourceState(void* d3d12Resource) const;

    [[nodiscard]] TargetDimensions GetDimensions() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_dims;
    }

    bool IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const;
    bool IsQuarterPassCandidate(uint32_t width, uint32_t height) const;

    // Vulkan attachment & blit safety checks (Items C-01 & C-04)
    [[nodiscard]] static bool IsVulkanDepthFormat(int32_t format);
    [[nodiscard]] static bool IsDepthCopyFormatCompatible(uint32_t srcFormat, uint32_t dstFormat);
    [[nodiscard]] static bool IsColorCopyFormatCompatible(uint32_t srcFormat, uint32_t dstFormat);
    [[nodiscard]] bool CanCopyToQuarterColor(const VulkanImageMetadata& srcMeta) const;
    [[nodiscard]] bool CanCopyToQuarterDepth(const VulkanImageMetadata& srcMeta) const;
    [[nodiscard]] bool CanCopyToVelocity(const VulkanImageMetadata& srcMeta) const;
    [[nodiscard]] bool ShouldBlitReconstructionToSceneColor(const void* gameColorImage,
                                                            bool isSwapchainBackbuffer,
                                                            uint32_t passWidth,
                                                            uint32_t passHeight,
                                                            const VulkanImageMetadata& dstMeta = {}) const;

    // Ping-pong history buffer index management
    [[nodiscard]] uint32_t GetCurrentHistoryIndex() const { return m_historyPingPong.load(); }
    [[nodiscard]] uint32_t GetPreviousHistoryIndex() const { return 1u - m_historyPingPong.load(); }
    void     SwapHistoryBuffers() { m_historyPingPong.fetch_xor(1u); }
    void     ResetHistory() { m_historyPingPong.store(0u); }

    // Memory footprint tracking
    [[nodiscard]] size_t GetTotalAllocatedVramBytes() const { return m_totalAllocatedVramBytes.load(); }

private:
    RenderTargetManager() = default;
    ~RenderTargetManager() = default;

    mutable std::shared_mutex m_mutex;
    TargetDimensions          m_dims;
    VulkanRenderTargetSet     m_vkResources{};
    void*                     m_vkDevice{ nullptr };
    std::atomic<bool>         m_vkResourcesCreated{ false };
    DX12RenderTargetSet       m_dx12Resources{};
    void*                     m_dx12Device{ nullptr };
    std::atomic<bool>         m_dx12ResourcesCreated{ false };
    std::atomic<uint32_t>     m_historyPingPong{ 0 };
    std::atomic<size_t>       m_totalAllocatedVramBytes{ 0 };
    std::atomic<bool>         m_initialized{ false };
};

} // namespace cbr
