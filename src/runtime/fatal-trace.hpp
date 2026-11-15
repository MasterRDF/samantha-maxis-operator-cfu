#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string_view>

// Observe the existing Kronos fatal-error chain, then forward all four
// arguments and the return value. No engine code or validation is changed.
namespace fatal_trace {
using Function = uintptr_t (__fastcall*)(int, int, const char*, void*);
inline std::atomic<Function> original{nullptr};
inline uintptr_t engineBase = 0;
inline std::filesystem::path directory;
inline std::atomic_flag recorded = ATOMIC_FLAG_INIT;
inline std::atomic_flag targetRecorded = ATOMIC_FLAG_INIT;
inline std::atomic<bool> modRequestActive{false};
inline void (*errorNotification)(uint32_t,int,const char*) noexcept = nullptr;

inline std::string hex(uintptr_t value) {
    std::ostringstream out; out << std::hex << value; return out.str();
}
inline bool writable(const void* address, size_t length) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address, &region, sizeof region) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = region.Protect & 0xff;
    return (p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY) &&
        reinterpret_cast<uintptr_t>(address) + length <= reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
}
inline bool executable(const void* address) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address, &region, sizeof region) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = region.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}
inline unsigned capturePages(std::ostream& log, uintptr_t base, DWORD first, DWORD size, const std::filesystem::path& folder) {
    if (!size || size > 65536 || (first & 4095) || (size & 4095) ||
        static_cast<uint64_t>(first) + size > 0xffffffffull) {
        log << "Page range refused.\n"; return 0;
    }
    unsigned saved = 0;
    for (DWORD offset = 0; offset < size; offset += 4096) {
        const DWORD rva = first + offset;
        std::array<char, 4096> bytes{}; SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(base + rva),
                               bytes.data(), bytes.size(), &read) || read != bytes.size()) {
            const auto error = GetLastError();
            log << "Page unreadable exe+0x" << hex(rva) << " Windows=" << error << "\n";
            continue;
        }
        const auto name = "bo7-runtime-code-" + hex(rva) + ".bin";
        std::ofstream dump(folder / name, std::ios::binary | std::ios::trunc);
        dump.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (dump) {
            ++saved;
            log << "frame page pc=0x" << hex(base + rva) << " exe+0x" << hex(rva)
                << " function=0x" << hex(rva) << "..0x" << hex(rva + bytes.size()) << " dump=" << name << "\n";
        }
    }
    return saved;
}
inline bool captureData(std::ostream& log, uintptr_t address, size_t size, const char* name,
                        const std::filesystem::path& folder) {
    if (!address || !size || size > 4096 || address > UINTPTR_MAX - size) return false;
    std::vector<char> bytes(size); SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                           bytes.data(), size, &read) || read != size) {
        log << "Data unreadable " << name << " address=0x" << hex(address) << "\n"; return false;
    }
    std::ofstream dump(folder / name, std::ios::binary | std::ios::trunc);
    dump.write(bytes.data(), static_cast<std::streamsize>(size));
    if (!dump) return false;
    log << "data address=0x" << hex(address) << " bytes=" << size << " dump=" << name << "\n";
    return true;
}
inline std::string boundedText(const char* source) {
    std::string text;
    if (!source) return text;
    const auto address = reinterpret_cast<uintptr_t>(source);
    for (size_t index = 0; index < 511 && address <= UINTPTR_MAX - index; ++index) {
        char value = 0; SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address + index), &value, 1, &got) || got != 1) {
            text += " [unreadable]"; break;
        }
        if (!value) break;
        text += value;
    }
    return text;
}
inline void capture(int code, int level, const char* message, void* extra) noexcept {
    try {
        std::ofstream log(directory / L"bo7-runtime-fatal-trace.log", std::ios::out | std::ios::trunc);
        if (!log) return;
        log << "Fatal trace v0.1; code=0x" << hex(static_cast<uint32_t>(code)) << " level=" << level
            << "; exeBase=0x" << hex(engineBase) << "; PID=" << GetCurrentProcessId()
            << "; thread=" << GetCurrentThreadId() << "\n";
        log << "message=" << boundedText(message) << "; extra=0x" << hex(reinterpret_cast<uintptr_t>(extra)) << "\n";
        // Exact CL29269900 read-only request data. Retain the bytes following
        // empty names so the lifetime guard can be diagnosed without weakening it.
        for (unsigned index = 0; index < 11; ++index) {
            const auto name = "bo7-runtime-error-request-" + std::to_string(index) + ".bin";
            captureData(log, engineBase + 0x13197200 + index * 0x1b8, 0x1b8, name.c_str(), directory);
        }
        captureData(log, engineBase + 0x131971b8, 8, "bo7-runtime-error-name-cell.bin", directory);
        captureData(log, engineBase + 0xdb43e90, 32, "bo7-runtime-error-descriptor.bin", directory);
        std::array<void*, 24> frames{};
        const USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
        std::vector<DWORD> saved;
        for (USHORT i = 0; i < count; ++i) {
            const auto pc = reinterpret_cast<uintptr_t>(frames[i]);
            DWORD64 image = 0;
            auto entry = RtlLookupFunctionEntry(pc, &image, nullptr);
            log << "frame " << i << " pc=0x" << hex(pc);
            if (image == engineBase && pc >= engineBase) {
                log << " exe+0x" << hex(pc - engineBase);
                if (entry) {
                    const DWORD begin = entry->BeginAddress, end = entry->EndAddress;
                    log << " function=0x" << hex(begin) << "..0x" << hex(end);
                    if (end > begin && end - begin <= 65536 && saved.size() < 8 &&
                        std::find(saved.begin(), saved.end(), begin) == saved.end()) {
                        std::vector<char> bytes(end - begin);
                        SIZE_T read = 0;
                        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(engineBase + begin),
                                              bytes.data(), bytes.size(), &read) && read == bytes.size()) {
                            const auto name = "bo7-runtime-code-" + hex(begin) + ".bin";
                            std::ofstream dump(directory / name, std::ios::binary | std::ios::trunc);
                            dump.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                            if (dump) { log << " dump=" << name; saved.push_back(begin); }
                        }
                    }
                }
            } else if (image) log << " moduleBase=0x" << hex(image) << " +0x" << hex(pc - image);
            log << "\n";
        }
        if (std::find(saved.begin(), saved.end(), 0x3a4d350u) != saved.end()) {
            const auto pages = capturePages(log, engineBase, 0x3a4b000, 65536, directory);
            log << "Fastfile reader pages captured: " << pages << "/16.\n";
            const auto headerPages = capturePages(log, engineBase, 0x3a46000, 0x5000, directory);
            log << "Fastfile input pages captured: " << headerPages << "/5.\n";
            const auto filePages = capturePages(log, engineBase, 0x3d6c000, 0x2000, directory);
            log << "File status pages captured: " << filePages << "/2.\n";
        }
        CONTEXT context{};
        RtlCaptureContext(&context);
        for (unsigned i = 0; i < 24 && context.Rip && context.Rsp; ++i) {
            DWORD64 image = 0;
            auto function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (image == engineBase) {
                log << "unwind " << i << " exe+0x" << hex(context.Rip - image)
                    << " rsp=0x" << hex(context.Rsp) << " rbx=0x" << hex(context.Rbx)
                    << " rbp=0x" << hex(context.Rbp) << " rsi=0x" << hex(context.Rsi)
                    << " rdi=0x" << hex(context.Rdi) << " r12=0x" << hex(context.R12)
                    << " r13=0x" << hex(context.R13) << " r14=0x" << hex(context.R14)
                    << " r15=0x" << hex(context.R15) << "\n";
                // At this precise call site R12 still holds the unresolved
                // serialized field and RDI the loader. Observe bounded data;
                // the original pointer check and fatal call stay unchanged.
                if (context.Rip - image == 0x382c81f) {
                    captureData(log, context.R12, 64, "bo7-runtime-rewind-field.bin", directory);
                    captureData(log, context.Rdi, 0xc40, "bo7-runtime-load-context.bin", directory);
                    captureData(log, context.Rsi, 128, "bo7-runtime-asset-context.bin", directory);
                }
            }
            const auto oldRsp = context.Rsp;
            if (function) {
                void* handler = nullptr; DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &frame, nullptr);
            } else {
                SIZE_T countRead = 0; DWORD64 next = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(context.Rsp), &next, sizeof next, &countRead) || countRead != sizeof next) break;
                context.Rip = next; context.Rsp += 8;
            }
            if (context.Rsp <= oldRsp || context.Rsp - oldRsp > 0x100000) break;
        }
        log << "Forwarding the original fatal-error call unchanged.\n";
    } catch (...) { /* Diagnostics must not replace the original error path. */ }
}
inline bool targetedError(int code, const char* message) noexcept {
    if (modRequestActive.load()) return true;
    if (static_cast<uint32_t>(code) == 0x05466e03u) return true;
    if (static_cast<uint32_t>(code) == 0xa330e056u) return true;
    if (!message) return false;
    std::array<char, 512> bytes{}; SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), message, bytes.data(), bytes.size(), &read) || read != bytes.size()) return false;
    const auto end = std::find(bytes.begin(), bytes.end(), '\0');
    return std::string_view(bytes.data(), static_cast<size_t>(end - bytes.begin())).find("bo3_bo7-runtime") != std::string_view::npos;
}
__declspec(noinline) inline uintptr_t __fastcall observe(int code, int level, const char* message, void* extra) {
    const DWORD prior = GetLastError();
    const bool exact = static_cast<uint32_t>(code) == 0x05466e03u;
    if (targetedError(code, message) && (exact ? !targetRecorded.test_and_set() :
        (!targetRecorded.test() && !recorded.test_and_set()))) capture(code, level, message, extra);
    if(errorNotification&&modRequestActive.load())errorNotification(static_cast<uint32_t>(code),level,message);
    SetLastError(prior);
    return original.load(std::memory_order_acquire)(code, level, message, extra);
}
inline bool install(void* volatile* cell, Function expected, uintptr_t base, const std::filesystem::path& output) {
    if (original.load() || !expected || !cell || (reinterpret_cast<uintptr_t>(cell) & (alignof(void*) - 1)) ||
        !writable(const_cast<void**>(cell), sizeof(void*)) || !executable(reinterpret_cast<void*>(expected))) return false;
    directory = output;
    engineBase = base;
    original.store(expected, std::memory_order_release);
    const auto previous = InterlockedCompareExchangePointer(cell, reinterpret_cast<void*>(&observe), reinterpret_cast<void*>(expected));
    if (previous != reinterpret_cast<void*>(expected)) { original.store(nullptr); return false; }
    return true;
}
}
