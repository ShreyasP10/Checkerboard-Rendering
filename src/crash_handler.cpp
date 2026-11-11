#include "cbr/crash_handler.h"
#include "cbr/logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace cbr {

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
std::filesystem::path        g_crashLogDirectory;
HANDLE                       g_hCrashLogFile = INVALID_HANDLE_VALUE;
HANDLE                       g_hMainLogFile  = INVALID_HANDLE_VALUE;
constexpr size_t             kMaxCrashDumpPathLength = 1024;
wchar_t                      g_dumpPathW[kMaxCrashDumpPathLength] = { 0 };

typedef BOOL(WINAPI* PFN_MiniDumpWriteDump)(
    HANDLE                            hProcess,
    DWORD                             ProcessId,
    HANDLE                            hFile,
    DWORD                             DumpType,
    const void*                       ExceptionParam,
    const void*                       UserStreamParam,
    const void*                       CallbackParam
);

HMODULE                      g_hDbgHelp = nullptr;
PFN_MiniDumpWriteDump        g_pfnMiniDumpWriteDump = nullptr;

const char* GetExceptionCodeString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION (Memory Access Violation)";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (Array Out of Bounds)";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT (Hardcoded Breakpoint)";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT (Unaligned Data Access)";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND (Floating-point Denormal)";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO (Float Division by Zero)";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT (Float Inexact Result)";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION (Float Invalid Operation)";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW (Float Overflow)";
        case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK (Float Stack Check)";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW (Float Underflow)";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION (Invalid CPU Instruction)";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR (Paging I/O Error)";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO (Integer Division by Zero)";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW (Integer Overflow)";
        case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION (Invalid Exception Disposition)";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION (Cannot Continue)";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION (Privileged Instruction)";
        case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP (Single Step)";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW (Stack Exhaustion)";
        default:                                 return "UNKNOWN_OR_CUSTOM_EXCEPTION";
    }
}

// Low-level, stack-only address resolver (async-signal-safe: no heap allocation)
void FormatModuleAndOffset(void* address, char* outBuf, size_t outSize) {
    if (!outBuf || outSize == 0) return;
    if (!address) {
        std::snprintf(outBuf, outSize, "null");
        return;
    }

    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),
        &hModule) && hModule) {

        char modPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(hModule, modPath, MAX_PATH);

        const char* modName = modPath;
        for (const char* p = modPath; *p; ++p) {
            if (*p == '\\' || *p == '/') modName = p + 1;
        }

        const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(hModule);
        const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(address);
        const uintptr_t offset = targetAddr - baseAddr;

        std::snprintf(outBuf, outSize, "%s + 0x%zX [Base: 0x%zX]", modName, offset, baseAddr);
        return;
    }

    std::snprintf(outBuf, outSize, "0x%zX (unmapped)", reinterpret_cast<uintptr_t>(address));
}

void WriteMinidump(EXCEPTION_POINTERS* pEx, const wchar_t* dumpPath) {
    // Use preloaded MiniDumpWriteDump pointer cached during CrashHandler::Install()
    // so the unhandled exception filter never acquires the PEB loader lock (Section 4.2).
    PFN_MiniDumpWriteDump pfnDump = g_pfnMiniDumpWriteDump;
    if (!pfnDump) return;

    {
        HANDLE hFile = CreateFileW(
            dumpPath,
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile != INVALID_HANDLE_VALUE) {
            // Struct for MiniDumpWriteDump MINIDUMP_EXCEPTION_INFORMATION
            // Packed with 4-byte alignment according to Microsoft dbghelp ABI (pshpack4.h / Item 89)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
#pragma pack(push, 4)
            struct MinimalMiniDumpExceptionInformation {
                DWORD ThreadId;
                PEXCEPTION_POINTERS ExceptionPointers;
                BOOL ClientPointers;
            } expInfo{};
#pragma pack(pop)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
            static_assert(sizeof(expInfo) == (sizeof(void*) == 8 ? 16 : 12),
                          "MINIDUMP_EXCEPTION_INFORMATION packing ABI drift");

            expInfo.ThreadId = GetCurrentThreadId();
            expInfo.ExceptionPointers = pEx;
            expInfo.ClientPointers = FALSE;

            constexpr DWORD kMiniDumpNormal = 0x00000000;
            constexpr DWORD kMiniDumpWithDataSegs = 0x00000001;
            constexpr DWORD kMiniDumpWithIndirectlyReferencedMemory = 0x00000040;

            pfnDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                hFile,
                kMiniDumpNormal | kMiniDumpWithDataSegs | kMiniDumpWithIndirectlyReferencedMemory,
                &expInfo,
                nullptr,
                nullptr);

            CloseHandle(hFile);
        }
    }
}

// Stack report builder appending helper (no heap allocations)
void AppendToReport(char* buf, size_t bufSize, size_t& offset, const char* format, ...) {
    if (offset >= bufSize - 1) return;
    va_list args;
    va_start(args, format);
    int written = std::vsnprintf(buf + offset, bufSize - offset, format, args);
    va_end(args);
    if (written > 0) {
        offset = (offset + written < bufSize) ? offset + written : bufSize - 1;
    }
}

LONG WINAPI CBRUnhandledExceptionFilter(EXCEPTION_POINTERS* pEx) {
    if (!pEx || !pEx->ExceptionRecord) {
        return (g_previousFilter) ? g_previousFilter(pEx) : EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD code = pEx->ExceptionRecord->ExceptionCode;
    void* faultAddress = pEx->ExceptionRecord->ExceptionAddress;

    // Pre-allocated stack buffer: completely async-signal-safe, 0 heap allocations
    char reportBuf[8192];
    size_t offset = 0;
    reportBuf[0] = '\0';

    AppendToReport(reportBuf, sizeof(reportBuf), offset,
        "\n=================================================================\n"
        " [FATAL] UNHANDLED EXCEPTION / GAME CRASH DETECTED\n"
        "=================================================================\n");

    char faultModBuf[256] = { 0 };
    FormatModuleAndOffset(faultAddress, faultModBuf, sizeof(faultModBuf));

    AppendToReport(reportBuf, sizeof(reportBuf), offset,
        "Exception Code:    0x%08lX (%s)\n"
        "Fault Address:     0x%zX in %s\n",
        code, GetExceptionCodeString(code),
        reinterpret_cast<uintptr_t>(faultAddress), faultModBuf);

    // Access violation details: Read, Write, or Execute
    if (code == EXCEPTION_ACCESS_VIOLATION && pEx->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR accessType = pEx->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR targetAddr = pEx->ExceptionRecord->ExceptionInformation[1];
        const char* typeStr = "access";
        if (accessType == 0) typeStr = "read from";
        else if (accessType == 1) typeStr = "write to";
        else if (accessType == 8) typeStr = "execute at";

        AppendToReport(reportBuf, sizeof(reportBuf), offset,
            "Violation Details: The thread attempted to %s invalid virtual address 0x%zX\n",
            typeStr, static_cast<uintptr_t>(targetAddr));
    }

#if defined(_M_X64) || defined(__x86_64__)
    if (pEx->ContextRecord) {
        const auto& ctx = *pEx->ContextRecord;
        AppendToReport(reportBuf, sizeof(reportBuf), offset,
            "\nCPU Register Dump (x64):\n"
            "  RAX: 0x%016llX  RBX: 0x%016llX  RCX: 0x%016llX\n"
            "  RDX: 0x%016llX  RSI: 0x%016llX  RDI: 0x%016llX\n"
            "  RBP: 0x%016llX  RSP: 0x%016llX  RIP: 0x%016llX\n"
            "  R8:  0x%016llX  R9:  0x%016llX  R10: 0x%016llX\n"
            "  R11: 0x%016llX  R12: 0x%016llX  R13: 0x%016llX\n"
            "  R14: 0x%016llX  R15: 0x%016llX  EFL: 0x%08lX\n",
            ctx.Rax, ctx.Rbx, ctx.Rcx,
            ctx.Rdx, ctx.Rsi, ctx.Rdi,
            ctx.Rbp, ctx.Rsp, ctx.Rip,
            ctx.R8, ctx.R9, ctx.R10,
            ctx.R11, ctx.R12, ctx.R13,
            ctx.R14, ctx.R15, ctx.EFlags);
    }
#endif

    // Call stack backtrace (return addresses)
    AppendToReport(reportBuf, sizeof(reportBuf), offset, "\nCall Stack Backtrace (Return Addresses):\n");
    void* stackFrames[32] = { 0 };
    USHORT captured = CaptureStackBackTrace(0, 32, stackFrames, nullptr);
    for (USHORT i = 0; i < captured; ++i) {
        char frameModBuf[256] = { 0 };
        FormatModuleAndOffset(stackFrames[i], frameModBuf, sizeof(frameModBuf));
        AppendToReport(reportBuf, sizeof(reportBuf), offset, "  [%02u] %s\n", i, frameModBuf);
    }

    // Memory status
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        AppendToReport(reportBuf, sizeof(reportBuf), offset,
            "\nSystem Memory at Crash:\n"
            "  Memory Load:     %u%%\n"
            "  Available RAM:   %llu MB / %llu MB\n"
            "  Available Page:  %llu MB\n",
            mem.dwMemoryLoad,
            static_cast<unsigned long long>(mem.ullAvailPhys / (1024 * 1024)),
            static_cast<unsigned long long>(mem.ullTotalPhys / (1024 * 1024)),
            static_cast<unsigned long long>(mem.ullAvailPageFile / (1024 * 1024)));
    }

    AppendToReport(reportBuf, sizeof(reportBuf), offset,
        "=================================================================\n");

    // 1. Output to dedicated cbr_crash.log via pre-opened handle (or fallback direct handle)
    if (g_hCrashLogFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_hCrashLogFile, reportBuf, static_cast<DWORD>(offset), &written, nullptr);
        FlushFileBuffers(g_hCrashLogFile);
    }

    // 2. Output to cbr.log via pre-opened handle without touching Logger mutex or heap
    if (g_hMainLogFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_hMainLogFile, reportBuf, static_cast<DWORD>(offset), &written, nullptr);
        FlushFileBuffers(g_hMainLogFile);
    }

    // 3. Write minidump (.dmp) for Visual Studio / WinDbg debugging
    if (g_dumpPathW[0] != L'\0') {
        WriteMinidump(pEx, g_dumpPathW);
    }

    if (g_previousFilter) {
        return g_previousFilter(pEx);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

CrashHandler& CrashHandler::Get() {
    static CrashHandler instance;
    return instance;
}

void CrashHandler::Install(const std::filesystem::path& logDirectory) {
    if (m_installed) return;
    m_logDirectory = logDirectory;
    g_crashLogDirectory = logDirectory;

    // Pre-open handles to cbr_crash.log and cbr.log for zero-allocation, deadlock-free crash writing
    const std::filesystem::path crashLog = logDirectory / "cbr_crash.log";
    g_hCrashLogFile = CreateFileW(crashLog.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    const std::filesystem::path mainLog = logDirectory / "cbr.log";
    g_hMainLogFile = CreateFileW(mainLog.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    const std::filesystem::path dumpPath = logDirectory / "cbr_crash.dmp";
    const auto& nativeDump = dumpPath.native();
    const size_t copyLen = (std::min)(nativeDump.size(), kMaxCrashDumpPathLength - 1);
    std::copy_n(nativeDump.c_str(), copyLen, g_dumpPathW);
    g_dumpPathW[copyLen] = L'\0';

    // Preload dbghelp.dll and cache MiniDumpWriteDump once during Install() so exception
    // handling never acquires the PEB loader lock inside CBRUnhandledExceptionFilter (Section 4.2).
    if (!g_hDbgHelp) {
        g_hDbgHelp = LoadLibraryExA("dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (g_hDbgHelp && !g_pfnMiniDumpWriteDump) {
        g_pfnMiniDumpWriteDump = reinterpret_cast<PFN_MiniDumpWriteDump>(
            GetProcAddress(g_hDbgHelp, "MiniDumpWriteDump"));
    }

    g_previousFilter = SetUnhandledExceptionFilter(CBRUnhandledExceptionFilter);
    m_installed = true;
    CBR_LOG_INFO("CrashHandler installed. Unhandled exceptions & crash telemetry will be logged to cbr_crash.log.");
}

void CrashHandler::Uninstall() {
    if (!m_installed) return;
    SetUnhandledExceptionFilter(g_previousFilter);
    g_previousFilter = nullptr;

    g_pfnMiniDumpWriteDump = nullptr;
    if (g_hDbgHelp) {
        FreeLibrary(g_hDbgHelp);
        g_hDbgHelp = nullptr;
    }

    if (g_hCrashLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hCrashLogFile);
        g_hCrashLogFile = INVALID_HANDLE_VALUE;
    }
    if (g_hMainLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hMainLogFile);
        g_hMainLogFile = INVALID_HANDLE_VALUE;
    }
    g_dumpPathW[0] = L'\0';

    m_installed = false;
    CBR_LOG_INFO("CrashHandler uninstalled.");
}

void CrashHandler::LogSystemTelemetry() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    const DWORD pid = GetCurrentProcessId();

    CBR_LOG_INFO("--- Environment & System Telemetry ---");
    CBR_LOG_INFO("Host Process:        %s (PID: %lu)", exePath, pid);

    SYSTEM_INFO sysInfo{};
    GetNativeSystemInfo(&sysInfo);
    CBR_LOG_INFO("CPU Architecture:    %s (%u logical processor cores)",
        sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64 (AMD64)" : "x86/Other",
        sysInfo.dwNumberOfProcessors);

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        CBR_LOG_INFO("Physical Memory:     %llu MB Total, %llu MB Free (%u%% in use)",
            static_cast<unsigned long long>(mem.ullTotalPhys / (1024 * 1024)),
            static_cast<unsigned long long>(mem.ullAvailPhys / (1024 * 1024)),
            mem.dwMemoryLoad);
    }

    // Detect graphic runtimes & relevant hooks
    HMODULE hVk    = GetModuleHandleA("vulkan-1.dll");
    HMODULE hD3D12 = GetModuleHandleA("d3d12.dll");
    HMODULE hDXGI  = GetModuleHandleA("dxgi.dll");
    HMODULE hNvApi = GetModuleHandleA("nvapi64.dll");
    HMODULE hAmdAgs= GetModuleHandleA("amd_ags_x64.dll");
    HMODULE hAsi   = GetModuleHandleA("dinput8.dll");
    HMODULE hScript= GetModuleHandleA("ScriptHookRDR2.dll");

    CBR_LOG_INFO("Vulkan Runtime:      %s", hVk ? "Loaded (vulkan-1.dll present)" : "Not Loaded");
    CBR_LOG_INFO("DirectX 12 Runtime:  %s", hD3D12 ? "Loaded (d3d12.dll present)" : "Not Loaded");
    CBR_LOG_INFO("DXGI Runtime:        %s", hDXGI ? "Loaded (dxgi.dll present)" : "Not Loaded");
    CBR_LOG_INFO("NVIDIA API:          %s", hNvApi ? "Loaded (nvapi64.dll)" : "Not Present");
    CBR_LOG_INFO("AMD AGS:             %s", hAmdAgs ? "Loaded (amd_ags_x64.dll)" : "Not Present");
    CBR_LOG_INFO("ASI Loader:          %s", hAsi ? "Detected (dinput8.dll)" : "Not Detected");
    CBR_LOG_INFO("ScriptHook:          %s", hScript ? "Detected (ScriptHookRDR2.dll)" : "Not Detected");
    CBR_LOG_INFO("--------------------------------------");
    Logger::Get().Flush();
}

} // namespace cbr

#else

// Non-Windows stub for portable builds & tests
namespace cbr {

CrashHandler& CrashHandler::Get() {
    static CrashHandler instance;
    return instance;
}

void CrashHandler::Install(const std::filesystem::path& logDirectory) {
    m_logDirectory = logDirectory;
    m_installed = true;
}

void CrashHandler::Uninstall() {
    m_installed = false;
}

void CrashHandler::LogSystemTelemetry() {
    CBR_LOG_INFO("CrashHandler: portable non-Windows build telemetry.");
}

} // namespace cbr

#endif
