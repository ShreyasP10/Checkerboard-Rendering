#include "cbr/hooks.h"
#include "cbr/logger.h"

namespace cbr {

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

bool HookManager::Initialize() {
    CBR_LOG_INFO("HookManager initialized.");
    return true;
}

void HookManager::Shutdown() {
    UninstallVulkanHooks();
    UninstallDX12Hooks();
    CBR_LOG_INFO("HookManager shut down.");
}

} // namespace cbr
