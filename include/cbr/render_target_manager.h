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

    [[nodiscard]] TargetDimensions GetDimensions() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_dims;
    }

    bool IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const;
    bool IsQuarterPassCandidate(uint32_t width, uint32_t height) const;

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
    std::atomic<uint32_t>     m_historyPingPong{ 0 };
    std::atomic<size_t>       m_totalAllocatedVramBytes{ 0 };
    std::atomic<bool>         m_initialized{ false };
};

} // namespace cbr
