#include "cbr/pattern_scanner.h"
#include <atomic>
#include <cctype>
#include <cstring>
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi"
#endif

namespace cbr {

namespace {

bool DetectAVX2() {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
    int info[4] = { 0 };
    __cpuid(info, 0);
    if (info[0] >= 7) {
        __cpuidex(info, 7, 0);
        return (info[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
    }
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__builtin_cpu_supports)
    return __builtin_cpu_supports("avx2");
    #else
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1u << 5)) != 0;
    }
    #endif
#endif
#endif
    return false;
}

const bool g_hasAVX2 = DetectAVX2();

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC push_options
#pragma GCC target("avx2")
#endif

#if defined(__x86_64__) || defined(_M_X64)
const uint8_t* FindPatternAVX2(const uint8_t* base, size_t size, const char* signature, const char* mask, size_t maskLen) {
    const size_t scanLimit = size - maskLen;
    const uint8_t firstByte = static_cast<uint8_t>(signature[0]);
    const __m256i target = _mm256_set1_epi8(static_cast<char>(firstByte));

    size_t i = 0;
    while (i + 32 <= scanLimit + 1) {
        const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(base + i));
        const __m256i cmp = _mm256_cmpeq_epi8(chunk, target);
        uint32_t maskBits = static_cast<uint32_t>(_mm256_movemask_epi8(cmp));

        while (maskBits != 0) {
#if defined(_MSC_VER)
            unsigned long bitIdx = 0;
            _BitScanForward(&bitIdx, maskBits);
#else
            uint32_t bitIdx = static_cast<uint32_t>(__builtin_ctz(maskBits));
#endif
            const size_t candidateIdx = i + bitIdx;
            if (candidateIdx <= scanLimit) {
                bool match = true;
                for (size_t j = 1; j < maskLen; ++j) {
                    if (mask[j] == 'x' && base[candidateIdx + j] != static_cast<uint8_t>(signature[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return base + candidateIdx;
                }
            }
            maskBits &= (maskBits - 1u);
        }
        i += 32;
    }

    for (; i <= scanLimit; ++i) {
        if (base[i] == firstByte) {
            bool match = true;
            for (size_t j = 1; j < maskLen; ++j) {
                if (mask[j] == 'x' && base[i + j] != static_cast<uint8_t>(signature[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return base + i;
        }
    }
    return nullptr;
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC target("sse2")
#endif

#if defined(__x86_64__) || defined(_M_X64)
const uint8_t* FindPatternSSE2(const uint8_t* base, size_t size, const char* signature, const char* mask, size_t maskLen) {
    const size_t scanLimit = size - maskLen;
    const uint8_t firstByte = static_cast<uint8_t>(signature[0]);
    const __m128i target = _mm_set1_epi8(static_cast<char>(firstByte));

    size_t i = 0;
    while (i + 16 <= scanLimit + 1) {
        const __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(base + i));
        const __m128i cmp = _mm_cmpeq_epi8(chunk, target);
        uint32_t maskBits = static_cast<uint32_t>(_mm_movemask_epi8(cmp));

        while (maskBits != 0) {
#if defined(_MSC_VER)
            unsigned long bitIdx = 0;
            _BitScanForward(&bitIdx, maskBits);
#else
            uint32_t bitIdx = static_cast<uint32_t>(__builtin_ctz(maskBits));
#endif
            const size_t candidateIdx = i + bitIdx;
            if (candidateIdx <= scanLimit) {
                bool match = true;
                for (size_t j = 1; j < maskLen; ++j) {
                    if (mask[j] == 'x' && base[candidateIdx + j] != static_cast<uint8_t>(signature[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return base + candidateIdx;
                }
            }
            maskBits &= (maskBits - 1u);
        }
        i += 16;
    }

    for (; i <= scanLimit; ++i) {
        if (base[i] == firstByte) {
            bool match = true;
            for (size_t j = 1; j < maskLen; ++j) {
                if (mask[j] == 'x' && base[i + j] != static_cast<uint8_t>(signature[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return base + i;
        }
    }
    return nullptr;
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC pop_options
#pragma GCC diagnostic pop
#endif

} // namespace

const uint8_t* PatternScanner::FindPattern(const uint8_t* base, size_t size, const char* signature, const char* mask) {
    if (!base || size == 0 || !signature || !mask) {
        return nullptr;
    }

    const size_t maskLen = std::strlen(mask);
    if (size < maskLen) {
        return nullptr;
    }

    const bool firstByteFixed = (mask[0] == 'x');
    if (firstByteFixed) {
#if defined(__x86_64__) || defined(_M_X64)
        if (g_hasAVX2 && (size - maskLen) >= 32) {
            return FindPatternAVX2(base, size, signature, mask, maskLen);
        } else if ((size - maskLen) >= 16) {
            return FindPatternSSE2(base, size, signature, mask, maskLen);
        }
#endif
    }

    const size_t scanLimit = size - maskLen;
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
                const auto* const secEnd = secBase + secSize;
                const uint8_t* cursor = secBase;
                constexpr DWORD kReadableProtectMask =
                    PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

                while (cursor < secEnd) {
                    MEMORY_BASIC_INFORMATION regMbi{};
                    if (VirtualQuery(cursor, &regMbi, sizeof(regMbi)) != sizeof(regMbi) ||
                        regMbi.RegionSize == 0) {
                        break;
                    }
                    const auto* regBase = reinterpret_cast<const uint8_t*>(regMbi.BaseAddress);
                    const auto* regEnd = regBase + regMbi.RegionSize;
                    if (regEnd <= cursor) {
                        break;
                    }
                    const auto* scanEnd = (regEnd < secEnd) ? regEnd : secEnd;
                    const size_t scanLen = static_cast<size_t>(scanEnd - cursor);

                    const bool isCommitted = (regMbi.State == MEM_COMMIT);
                    const bool isForbidden = (regMbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
                    const bool isReadable  = (regMbi.Protect & kReadableProtectMask) != 0;

                    if (isCommitted && !isForbidden && isReadable && scanLen > 0) {
                        const uint8_t* found = FindPattern(cursor, scanLen, signature, mask);
                        if (found) {
                            return found;
                        }
                    }
                    cursor = scanEnd;
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

bool PatternScanner::UpdateCameraJitter(uint32_t frameIndex, bool isVulkan) {
    float* cameraMatrix = g_cachedCameraMatrix.load(std::memory_order_acquire);
    if (!cameraMatrix) {
        if (!InitializeCameraScanner()) {
            return false;
        }
        cameraMatrix = g_cachedCameraMatrix.load(std::memory_order_acquire);
    }

    if (cameraMatrix) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(cameraMatrix, &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix memory unreadable or not committed; falling back to Viewport Redirection.");
            return false;
        }
        const uintptr_t matrixAddr = reinterpret_cast<uintptr_t>(cameraMatrix);
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd < matrixAddr || (regionEnd - matrixAddr) < (16 * sizeof(float))) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix buffer smaller than 4x4 float matrix; falling back to Viewport Redirection.");
            return false;
        }
#if defined(_MSC_VER)
        __try {
            if (JitterManager::Get().IsProjectionJitterApplied(cameraMatrix, frameIndex, isVulkan)) {
                return true;
            }
            JitterManager::Get().ApplyJitterToProjection(cameraMatrix, isVulkan);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_cachedCameraMatrix.store(nullptr, std::memory_order_release);
            CBR_LOG_WARN("Camera projection matrix access faulted; falling back to Viewport Redirection.");
            return false;
        }
#else
        try {
            if (JitterManager::Get().IsProjectionJitterApplied(cameraMatrix, frameIndex, isVulkan)) {
                return true;
            }
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
