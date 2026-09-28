#pragma once

#include <cstdint>
#include <string>

namespace cbr {

struct ReconstructionPushConstants {
    float    targetResolution[2];
    float    invTargetResolution[2];
    uint32_t frameIndex;
    float    depthTolerance;
    float    historyWeight;
    uint32_t debugView;
    uint32_t enableColorClamping;
    float    mipLodBias;
    float    padding[2];
};

class ReconstructionPass {
public:
    static ReconstructionPass& Get();

    bool InitializeVulkan(void* vkDevice, void* vkPhysicalDevice);
    bool InitializeDX12(void* d3d12Device);
    void Shutdown();

    // Dispatch compute shader
    void DispatchVulkan(void* vkCommandBuffer, uint32_t frameIndex);
    void DispatchDX12(void* d3d12GraphicsCommandList, uint32_t frameIndex);

    bool IsInitialized() const { return m_initialized; }

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    bool m_initialized{ false };
    bool m_isVulkan{ true };
};

} // namespace cbr
