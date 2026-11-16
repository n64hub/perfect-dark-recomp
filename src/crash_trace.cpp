// Prints where the program crashed to stderr, which the launcher's log and a player's report carry: the exception (or
// signal), the address it faulted on and, when that address is in the game's memory (RDRAM's reservation, 4 GB from
// recomp_pd_rdram), the N64 address the game code used, then on Windows each frame of the crashing thread's stack,
// named from PerfectDarkRecompiled.pdb beside the exe (tools/package_windows.sh puts it in the zip). Recompiled
// functions carry the decomp's names (syms/dump.toml: joy_reset, bgunTick, ...), so a trace points at the game code
// that crashed. Without the .pdb a frame is the module and the offset into it. Linux's release is
// stripped, so there it prints the signal and the addresses only.
//
// install_crash_trace() is called first thing in main. It returns the crash to the system afterwards, so the exit
// code stays the crash's (0xC0000005 for an access violation on Windows).

#include <cstdint>
#include <cstdio>

// Set by recomp_pd_relocate (src/pd_natives.cpp) once the game runs.
uint8_t* recomp_pd_rdram = nullptr;

namespace {

// RDRAM's reservation, the 4 GB from rdram (librecomp's allocation_size) N64Recomp's memory macros reach: KSEG0's
// 0x80000000 is its first byte, and an address under 0x80000000 (an unrelocated 0x7f000000 pointer, a null one) lands
// 2-4 GB in, past the RDRAM committed, so the N64 address is the offset plus 0x80000000 in 32 bits either way.
void print_n64_address(uintptr_t address) {
    uintptr_t base = reinterpret_cast<uintptr_t>(recomp_pd_rdram);
    if (base != 0 && address >= base && address - base < 0x100000000ull) {
        fprintf(stderr, ", N64 address 0x%08X", static_cast<uint32_t>(address - base + 0x80000000ull));
    }
}

} // namespace

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

namespace {

void print_stack(HANDLE process, HANDLE thread, CONTEXT context) {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (int i = 0; i < 48; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 pc = frame.AddrPC.Offset;
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, pc, &displacement, symbol)) {
            fprintf(stderr, "  %2d %s+0x%llX", i, symbol->Name, (unsigned long long)displacement);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(process, pc, &line_displacement, &line)) {
                fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
            }
            fprintf(stderr, "\n");
            continue;
        }
        // No symbol: the module and the offset into it, which the release's .pdb names later.
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(pc), &module) && module != nullptr) {
            GetModuleFileNameA(module, name, sizeof(name));
            const char* base = name;
            for (const char* c = name; *c != '\0'; c++) {
                if (*c == '\\' || *c == '/') {
                    base = c + 1;
                }
            }
            fprintf(stderr, "  %2d %s+0x%llX\n", i, base, (unsigned long long)(pc - reinterpret_cast<DWORD64>(module)));
        }
        else {
            fprintf(stderr, "  %2d %p\n", i, reinterpret_cast<void*>(pc));
        }
    }
}

LONG WINAPI print_crash(EXCEPTION_POINTERS* info) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH; // a second thread's crash, or one in here: the first one's trace is enough
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    fprintf(stderr, "[pd] crash: exception 0x%08lX at %p", record->ExceptionCode, record->ExceptionAddress);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        uintptr_t address = static_cast<uintptr_t>(record->ExceptionInformation[1]);
        const char* kind = record->ExceptionInformation[0] == 0 ? "reading" : record->ExceptionInformation[0] == 8 ?
            "executing" : "writing";
        fprintf(stderr, ", %s 0x%llX", kind, static_cast<unsigned long long>(address));
        print_n64_address(address);
    }
    fprintf(stderr, ", thread %lu\n", GetCurrentThreadId());
    fflush(stderr);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    print_stack(process, GetCurrentThread(), *info->ContextRecord);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void install_crash_trace() {
    SetUnhandledExceptionFilter(print_crash);
}

#elif defined(__linux__)
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace {

void print_crash(int signal, siginfo_t* info, void*) {
    fprintf(stderr, "[pd] crash: %s, address 0x%llX", strsignal(signal),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)));
    print_n64_address(reinterpret_cast<uintptr_t>(info->si_addr));
    fprintf(stderr, "\n");
    fflush(stderr);
    // SA_RESETHAND put the default back: returning faults again and ends the process with the signal.
}

} // namespace

void install_crash_trace() {
    struct sigaction action{};
    action.sa_sigaction = print_crash;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int signal : { SIGSEGV, SIGBUS, SIGILL, SIGFPE }) {
        sigaction(signal, &action, nullptr);
    }
}

#else
void install_crash_trace() {}
#endif
