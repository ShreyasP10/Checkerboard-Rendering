#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cbr {

class PatternScanner {
public:
    // Core scan method across any raw memory buffer.
    // 'signature' is raw bytes, 'mask' contains 'x' for exact match and '?' for wildcard.
    static const uint8_t* FindPattern(const uint8_t* base, size_t size, const char* signature, const char* mask);

    // Parses IDA-style pattern string (e.g. "48 8B 05 ? ? ? ? 48 8B 48 08")
    // into raw signature bytes and mask string ('x' / '?').
    static bool ParseIdaPattern(std::string_view idaPattern, std::vector<uint8_t>& outBytes, std::string& outMask);

    // Convenience method scanning memory with an IDA-style pattern string.
    static const uint8_t* FindIdaPattern(const uint8_t* base, size_t size, std::string_view idaPattern);

    // Resolves standard x64 RIP-relative offset:
    // instruction at `instructionAddress` of `instructionLength` bytes with 32-bit displacement at `displacementOffset`
    // Target = instructionAddress + instructionLength + *(int32_t*)(instructionAddress + displacementOffset)
    static const uint8_t* ResolveRipRelative(const uint8_t* instructionAddress, size_t instructionLength, size_t displacementOffset = 3);

#if defined(_WIN32)
    // Scans an entire loaded module (e.g. "RDR2.exe" or nullptr for current process executable)
    static const uint8_t* ScanModule(const char* moduleName, const char* signature, const char* mask);
    static const uint8_t* ScanModuleIda(const char* moduleName, std::string_view idaPattern);

    // Attempts to locate and cache the RAGE camera projection matrix pointer in RDR2
    static bool InitializeCameraScanner();
    static float* GetCameraProjectionMatrix();
    static bool UpdateCameraJitter(uint32_t frameIndex, bool isVulkan);
#endif
};

} // namespace cbr
