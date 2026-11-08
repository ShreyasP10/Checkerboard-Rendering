#include "cbr/pattern_scanner.h"
#include <atomic>
#include <cctype>
#include <cstring>
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

const uint8_t* PatternScanner::FindPattern(const uint8_t* base, size_t size, const char* signature, const char* mask) {
    if (!base || size == 0 || !signature || !mask) {
        return nullptr;
    }

    const size_t maskLen = std::strlen(mask);
    if (size < maskLen) {
        return nullptr;
    }

    const size_t scanLimit = size - maskLen;
    const bool firstByteFixed = (mask[0] == 'x');
    const uint8_t firstByte = static_cast<uint8_t>(signature[0]);

    for (size_t i = 0; i <= scanLimit; ) {
        if (firstByteFixed) {
            const void* hit = std::memchr(base + i, firstByte, scanLimit - i + 1);
            if (!hit) {
                return nullptr;
            }
            i = static_cast<const uint8_t*>(hit) - base;
        }

        bool match = true;
        for (size_t j = (firstByteFixed ? 1 : 0); j < maskLen; ++j) {
            if (mask[j] == 'x' && base[i + j] != static_cast<uint8_t>(signature[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return base + i;
        }
        ++i;
    }
    return nullptr;
}

bool PatternScanner::ParseIdaPattern(std::string_view idaPattern, std::vector<uint8_t>& outBytes, std::string& outMask) {
    outBytes.clear();
    outMask.clear();
    outBytes.reserve(64);
    outMask.reserve(64);

    size_t i = 0;
    const size_t n = idaPattern.size();

    while (i < n) {
        // Skip whitespace
        while (i < n && std::isspace(static_cast<unsigned char>(idaPattern[i]))) {
            ++i;
        }
        if (i >= n) break;

        if (idaPattern[i] == '?') {
            outBytes.push_back(0x00);
            outMask.push_back('?');
            ++i;
            if (i < n && idaPattern[i] == '?') {
                ++i; // consume second '?' in "??"
            }
        } else {
            // Hex byte
            if (i + 1 < n && std::isxdigit(static_cast<unsigned char>(idaPattern[i])) &&
                std::isxdigit(static_cast<unsigned char>(idaPattern[i + 1]))) {
                char hex[3] = { idaPattern[i], idaPattern[i + 1], '\0' };
                uint8_t byteVal = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
                outBytes.push_back(byteVal);
                outMask.push_back('x');
                i += 2;
            } else {
                return false; // Malformed hex sequence
            }
        }
    }

    return !outBytes.empty();
}

const uint8_t* PatternScanner::FindIdaPattern(const uint8_t* base, size_t size, std::string_view idaPattern) {
    std::vector<uint8_t> bytes;
    std::string mask;
    if (!ParseIdaPattern(idaPattern, bytes, mask)) {
        return nullptr;
    }
    return FindPattern(base, size, reinterpret_cast<const char*>(bytes.data()), mask.c_str());
}

const uint8_t* PatternScanner::ResolveRipRelative(const uint8_t* instructionAddress, size_t instructionLength, size_t displacementOffset) {
    if (!instructionAddress || instructionLength < displacementOffset + sizeof(int32_t)) {
        return nullptr;
    }

#if defined(_WIN32)
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(instructionAddress),
        &hMod) || !hMod) {
        return nullptr;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(instructionAddress, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return nullptr;
    }
#endif

    int32_t displacement = 0;
    std::memcpy(&displacement, instructionAddress + displacementOffset, sizeof(int32_t));
    const uint8_t* target = instructionAddress + instructionLength + displacement;

#if defined(_WIN32)
    if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return nullptr;
    }
#endif

    return target;
}

#if defined(_WIN32)
namespace {
    std::atomic<float*> g_cachedCameraMatrix{ nullptr };
    std::atomic<bool>   g_cameraScannerAttempted{ false };
}

const uint8_t* PatternScanner::ScanModule(const char* moduleName, const char* signature, const char* mask) {
    HMODULE hMod = moduleName ? GetModuleHandleA(moduleName) : GetModuleHandleA(nullptr);
    if (!hMod) return nullptr;

    MEMORY_BASIC_INFORMATION modMbi{};
    if (!VirtualQuery(hMod, &modMbi, sizeof(modMbi)) || modMbi.State != MEM_COMMIT ||
        (modMbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return nullptr;
    }

#if defined(_MSC_VER)
    __try {
#endif
        auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(hMod);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

        const auto* ntHeaderAddr = reinterpret_cast<const uint8_t*>(hMod) + dosHeader->e_lfanew;
        MEMORY_BASIC_INFORMATION ntMbi{};
        if (!VirtualQuery(ntHeaderAddr, &ntMbi, sizeof(ntMbi)) || ntMbi.State != MEM_COMMIT ||
            (ntMbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return nullptr;
        }

        auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(ntHeaderAddr);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

        const auto sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
        const WORD numSections = ntHeaders->FileHeader.NumberOfSections;

        for (WORD s = 0; s < numSections; ++s) {
            const auto& sec = sectionHeader[s];
            // Scan executable code sections (e.g. .text)
            if (sec.Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) {
                const auto* secBase = reinterpret_cast<const uint8_t*>(hMod) + sec.VirtualAddress;
                const size_t secSize = sec.Misc.VirtualSize > 0 ? sec.Misc.VirtualSize : sec.SizeOfRawData;

                MEMORY_BASIC_INFORMATION secMbi{};
                if (!VirtualQuery(secBase, &secMbi, sizeof(secMbi)) || secMbi.State != MEM_COMMIT ||
                    (secMbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                    continue;
                }

                const uint8_t* found = FindPattern(secBase, secSize, signature, mask);
                if (found) {
                    return found;
                }
            }
        }
#if defined(_MSC_VER)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Guard page or access violation encountered during pattern scan
        return nullptr;
    }
#endif
    return nullptr;
}

const uint8_t* PatternScanner::ScanModuleIda(const char* moduleName, std::string_view idaPattern) {
    std::vector<uint8_t> bytes;
    std::string mask;
    if (!ParseIdaPattern(idaPattern, bytes, mask)) {
        return nullptr;
    }
    return ScanModule(moduleName, reinterpret_cast<const char*>(bytes.data()), mask.c_str());
}

bool PatternScanner::InitializeCameraScanner() {
    bool expected = false;
    if (!g_cameraScannerAttempted.compare_exchange_strong(expected, true)) {
        return g_cachedCameraMatrix.load(std::memory_order_acquire) != nullptr;
    }

    // Known candidate camera signatures for RDR2 PC (1.0.1491.50+):
    // 1. mov rax, cs:g_cameraManager (48 8B 05 ? ? ? ?)
    // 2. mov rcx, cs:g_cameraManager (48 8B 0D ? ? ? ?)
    static const char* kCameraPatterns[] = {
        "48 8B 05 ? ? ? ? 48 8B 48 08",
        "48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01",
        "48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 88"
    };

    for (const char* pattern : kCameraPatterns) {
        const uint8_t* match = ScanModuleIda(nullptr, pattern);
        if (match) {
            const uint8_t* managerPtr = ResolveRipRelative(match, 7, 3);
            if (managerPtr) {
                CBR_LOG_INFO("Camera manager pointer resolved at 0x%p via pattern '%s'", managerPtr, pattern);
                // In game, g_cameraManager points to camera struct containing projection matrix at offset 0x30 / 0x40
                // Store base address for projection matrix access
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(managerPtr, &mbi, sizeof(mbi)) && (mbi.State == MEM_COMMIT)) {
                    void* targetManager = nullptr;
                    std::memcpy(&targetManager, managerPtr, sizeof(void*));
                    if (targetManager && VirtualQuery(targetManager, &mbi, sizeof(mbi)) && (mbi.State == MEM_COMMIT)) {
                        float* matrixPtr = reinterpret_cast<float*>(static_cast<uint8_t*>(targetManager) + 0x40);
                        g_cachedCameraMatrix.store(matrixPtr, std::memory_order_release);
                        CBR_LOG_INFO("RDR2 camera projection matrix acquired at 0x%p", matrixPtr);
                        return true;
                    }
                }
            }
        }
    }

    CBR_LOG_INFO("RAGE camera manager signature not matched in current process; utilizing Viewport Redirection (Strategy A).");
    return false;
}

float* PatternScanner::GetCameraProjectionMatrix() {
    return g_cachedCameraMatrix.load(std::memory_order_acquire);
}

bool PatternScanner::UpdateCameraJitter(uint32_t /*frameIndex*/, bool isVulkan) {
    float* cameraMatrix = g_cachedCameraMatrix.load(std::memory_order_acquire);
    if (!cameraMatrix) {
        if (!InitializeCameraScanner()) {
            return false;
        }
        cameraMatrix = g_cachedCameraMatrix.load(std::memory_order_acquire);
    }

    if (cameraMatrix) {
#if defined(_MSC_VER)
        __try {
            JitterManager::Get().ApplyJitterToProjection(cameraMatrix, isVulkan);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix access faulted; falling back to Viewport Redirection.");
            return false;
        }
#else
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(cameraMatrix, &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix memory unreadable or not committed; falling back to Viewport Redirection.");
            return false;
        }
        try {
            JitterManager::Get().ApplyJitterToProjection(cameraMatrix, isVulkan);
            return true;
        } catch (...) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix access faulted; falling back to Viewport Redirection.");
            return false;
        }
#endif
    }
    return false;
}
#endif

} // namespace cbr
