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

typedef BOOL(WINAPI* PFN_MiniDumpWriteDump)(
    HANDLE                            hProcess,
    DWORD                             ProcessId,
    HANDLE                            hFile,
    DWORD                             DumpType,
    const void*                       ExceptionParam,
    const void*                       UserStreamParam,
    const void*                       CallbackParam
);

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

std::string GetModuleAndOffset(void* address) {
    if (!address) return "null";

    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),
        &hModule) && hModule) {

        char modPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(hModule, modPath, MAX_PATH);

        std::string fullPath(modPath);
        std::string modName = fullPath.substr(fullPath.find_last_of("\\/") + 1);

        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(hModule);
        uintptr_t targetAddr = reinterpret_cast<uintptr_t>(address);
        uintptr_t offset = targetAddr - baseAddr;

        std::stringstream ss;
        ss << modName << " + 0x" << std::hex << std::uppercase << offset
           << " [Base: 0x" << baseAddr << "]";
        return ss.str();
    }

    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(address) << " (unmapped)";
    return ss.str();
}

void WriteMinidump(EXCEPTION_POINTERS* pEx, const std::filesystem::path& dumpPath) {
    HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
    if (!hDbgHelp) return;

    auto pfnDump = reinterpret_cast<PFN_MiniDumpWriteDump>(
        GetProcAddress(hDbgHelp, "MiniDumpWriteDump"));
    if (pfnDump) {
        HANDLE hFile = CreateFileW(
            dumpPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile != INVALID_HANDLE_VALUE) {
            // Struct for MiniDumpWriteDump MINIDUMP_EXCEPTION_INFORMATION
            struct {
                DWORD ThreadId;
                PEXCEPTION_POINTERS ExceptionPointers;
                BOOL ClientPointers;
            } expInfo{};

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
    FreeLibrary(hDbgHelp);
}

LONG WINAPI CBRUnhandledExceptionFilter(EXCEPTION_POINTERS* pEx) {
    if (!pEx || !pEx->ExceptionRecord) {
        return (g_previousFilter) ? g_previousFilter(pEx) : EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD code = pEx->ExceptionRecord->ExceptionCode;
    void* faultAddress = pEx->ExceptionRecord->ExceptionAddress;

    std::stringstream ss;
    ss << "\n=================================================================\n"
       << " [FATAL] UNHANDLED EXCEPTION / GAME CRASH DETECTED\n"
       << "=================================================================\n";

    ss << "Exception Code:    0x" << std::hex << std::uppercase << code
       << " (" << GetExceptionCodeString(code) << ")\n";
    ss << "Fault Address:     0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(faultAddress)
       << " in " << GetModuleAndOffset(faultAddress) << "\n";

    // Access violation details: Read, Write, or Execute
    if (code == EXCEPTION_ACCESS_VIOLATION && pEx->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR accessType = pEx->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR targetAddr = pEx->ExceptionRecord->ExceptionInformation[1];
        const char* typeStr = "access";
        if (accessType == 0) typeStr = "read from";
        else if (accessType == 1) typeStr = "write to";
        else if (accessType == 8) typeStr = "execute at";

        ss << "Violation Details: The thread attempted to " << typeStr
           << " invalid virtual address 0x" << std::hex << std::uppercase << targetAddr << "\n";
    }

#if defined(_M_X64) || defined(__x86_64__)
    if (pEx->ContextRecord) {
        const auto& ctx = *pEx->ContextRecord;
        ss << "\nCPU Register Dump (x64):\n";
        ss << "  RAX: 0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.Rax
           << "  RBX: 0x" << std::setw(16) << ctx.Rbx
           << "  RCX: 0x" << std::setw(16) << ctx.Rcx << "\n";
        ss << "  RDX: 0x" << std::setw(16) << ctx.Rdx
           << "  RSI: 0x" << std::setw(16) << ctx.Rsi
           << "  RDI: 0x" << std::setw(16) << ctx.Rdi << "\n";
        ss << "  RBP: 0x" << std::setw(16) << ctx.Rbp
           << "  RSP: 0x" << std::setw(16) << ctx.Rsp
           << "  RIP: 0x" << std::setw(16) << ctx.Rip << "\n";
        ss << "  R8:  0x" << std::setw(16) << ctx.R8
           << "  R9:  0x" << std::setw(16) << ctx.R9
           << "  R10: 0x" << std::setw(16) << ctx.R10 << "\n";
        ss << "  R11: 0x" << std::setw(16) << ctx.R11
           << "  R12: 0x" << std::setw(16) << ctx.R12
           << "  R13: 0x" << std::setw(16) << ctx.R13 << "\n";
        ss << "  R14: 0x" << std::setw(16) << ctx.R14
           << "  R15: 0x" << std::setw(16) << ctx.R15
           << "  EFL: 0x" << std::hex << ctx.EFlags << "\n";
    }
#endif

    // Call stack backtrace
    ss << "\nCall Stack Backtrace (Return Addresses):\n";
    void* stackFrames[32] = { 0 };
    USHORT captured = CaptureStackBackTrace(0, 32, stackFrames, nullptr);
    for (USHORT i = 0; i < captured; ++i) {
        ss << "  [" << std::setw(2) << std::setfill('0') << i << "] "
           << GetModuleAndOffset(stackFrames[i]) << "\n";
    }

    // Memory status
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        ss << "\nSystem Memory at Crash:\n";
        ss << "  Memory Load:     " << mem.dwMemoryLoad << "%\n";
        ss << "  Available RAM:   " << (mem.ullAvailPhys / (1024 * 1024)) << " MB / "
           << (mem.ullTotalPhys / (1024 * 1024)) << " MB\n";
        ss << "  Available Page:  " << (mem.ullAvailPageFile / (1024 * 1024)) << " MB\n";
    }

    ss << "=================================================================\n";
    const std::string report = ss.str();

    // 1. Output to standard cbr.log and flush immediately
    CBR_LOG_ERROR("%s", report.c_str());
    Logger::Get().Flush();

    // 2. Output to dedicated cbr_crash.log
    const std::filesystem::path crashLog = g_crashLogDirectory / "cbr_crash.log";
    std::ofstream crashFile(crashLog, std::ios::out | std::ios::app);
    if (crashFile.is_open()) {
        crashFile << report;
        crashFile.flush();
    }

    // 3. Write minidump (.dmp) for Visual Studio / WinDbg debugging
    const std::filesystem::path dumpPath = g_crashLogDirectory / "cbr_crash.dmp";
    WriteMinidump(pEx, dumpPath);
    CBR_LOG_INFO("Crash minidump written to: %s", dumpPath.string().c_str());
    Logger::Get().Flush();

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

    g_previousFilter = SetUnhandledExceptionFilter(CBRUnhandledExceptionFilter);
    m_installed = true;
    CBR_LOG_INFO("CrashHandler installed. Unhandled exceptions & crash telemetry will be logged to cbr_crash.log.");
}

void CrashHandler::Uninstall() {
    if (!m_installed) return;
    SetUnhandledExceptionFilter(g_previousFilter);
    g_previousFilter = nullptr;
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
