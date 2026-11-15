#pragma once
#include "engine-context.hpp"
#include "fatal-trace.hpp"

namespace shared_slot {
using WriteFn = BOOL (WINAPI*)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*);
inline constexpr uintptr_t KronosWriteImportRva = 0x14d198;
inline constexpr unsigned MaximumPublications = 64;
inline constexpr size_t NameOffset = 0xea, NameSize = 0x40, OwnerOffset = 0x1b0;
inline constexpr size_t NamesOffset = 0x100, DescriptorOffset = 0x200, DescriptorSize = 0x40;
inline constexpr uint64_t SettleMs = 1000;
inline constexpr char PartnerName[] = "global_stream_cfwd_mp";

inline Address privateDescriptor(Address page) { return page + DescriptorOffset; }
inline bool zoneName(const std::string& name) {
    return !name.empty() && name.size() < NameSize &&
        std::all_of(name.begin(), name.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_';
        });
}
inline std::string boundedText(Address address) noexcept {
    std::string text;
    for (size_t index = 0; index < NameSize; ++index) {
        char character = 0;
        SIZE_T bytesRead = 0;
        if (address > UINTPTR_MAX - index ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address + index),
                &character, 1, &bytesRead) || bytesRead != 1) return {};
        if (!character) return text;
        text.push_back(character);
    }
    return {};
}
inline bool idle(const Request& request) {
    return request[0x20] == 0 && (request[0x21] == 0 || request[0x21] == 3);
}
inline bool namesEmpty(const Request& request) {
    return request[NameOffset] == 0 && request[NameOffset + NameSize] == 0 &&
        request[NameOffset + 2 * NameSize] == 0;
}
inline bool preloadReady(const Table& table) {
    bool frontEnd = false, globalStream = false, globalShared = false;
    for (size_t index = 0; index < table.size(); ++index) {
        const auto& request = table[index];
        if (index == ExtraSlot || request[0x20] != 2 ||
            field<Address>(request, OwnerOffset) == 0) continue;
        for (size_t nameIndex = 0; nameIndex < 3; ++nameIndex) {
            const auto name = nameAt(request, nameIndex);
            frontEnd |= name == "frontend_ui";
            globalStream |= name == "global_stream_mp";
            globalShared |= name == "global_shared_mp";
        }
    }
    return frontEnd && globalStream && globalShared;
}
inline void writeDescriptor(const Memory& memory, const Layout& layout, Address page, Address partner) {
    std::array<unsigned char, DescriptorSize> descriptor{};
    memory.read(layout.descriptor, descriptor.data(), descriptor.size());
    const uint32_t count = 2;
    const Address names = page + NamesOffset;
    std::memcpy(descriptor.data() + 12, &count, sizeof count);
    std::memcpy(descriptor.data() + 16, &names, sizeof names);
    memory.put<Address>(names, partner);
    memory.put<Address>(names + sizeof(Address), page);
    memory.write(privateDescriptor(page), descriptor.data(), descriptor.size());
}
inline bool loaded(const Request& request, Address page, const std::string& zone) {
    if (request[0x20] != 2 || field<Address>(request, OwnerOffset) != privateDescriptor(page) ||
        nameAt(request, 0) != PartnerName || nameAt(request, 1) != zone) return false;
    const auto working = [&](size_t index) {
        const auto text = reinterpret_cast<const char*>(request.data() + 0x2a + index * NameSize);
        return std::string(text, strnlen_s(text, NameSize));
    };
    return working(0) == PartnerName && working(1) == zone && working(2).empty();
}

enum class Outcome { added, notReady, unexpected, limit };
struct Attempt { Outcome outcome; std::string detail; };
inline Attempt add(const Memory& memory, const Layout& layout, Address page,
    const std::string& zone, char first, unsigned& publications, bool allowFrontEnd = false) {
    validateDescriptor(memory, layout);
    const Address requestAddress = layout.requests + ExtraSlot * Stride;
    const auto table = snapshot(memory, layout);
    const auto& request = table[ExtraSlot];
    if (!idle(request)) return {Outcome::unexpected, "request 4 is active or transitioning; left untouched"};
    if (!zombieReady(table) && !(allowFrontEnd && preloadReady(table)))
        return {Outcome::notReady, "required base packs are not ready or front-end preloading is not enabled"};
    if (field<Address>(request, OwnerOffset) != layout.descriptor)
        return {Outcome::unexpected, "request 4 is not owned by the game's pack descriptor"};
    if (!namesEmpty(request)) return {Outcome::unexpected, "request 4 already has a name"};
    const auto tail = reinterpret_cast<const char*>(request.data() + NameOffset + 1);
    const auto partner = std::string(1, first) + std::string(tail, strnlen_s(tail, NameSize - 1));
    if (partner != PartnerName || partner == zone)
        return {Outcome::unexpected, "unexpected pack name"};
    const Address cell = memory.get<Address>(layout.nameCell);
    if (cell == 0 || cell == page || boundedText(cell) != partner)
        return {Outcome::unexpected, "the pack name cell does not hold the publisher's name"};
    if (!zoneName(zone) || boundedText(page) != zone)
        return {Outcome::unexpected, "the package name page changed"};
    if (publications >= MaximumPublications) return {Outcome::limit, "publication limit reached"};
    writeDescriptor(memory, layout, page, cell);
    if (memory.get<Request>(requestAddress) != request || memory.get<Address>(layout.nameCell) != cell)
        return {Outcome::unexpected, "request 4 changed before publication; left untouched"};
    std::array<char, NameSize> name{};
    std::memcpy(name.data(), zone.c_str(), zone.size());
    memory.write(requestAddress + NameOffset + NameSize + 1, name.data() + 1, NameSize - 1);
    memory.put<Address>(requestAddress + OwnerOffset, privateDescriptor(page));
    try {
        memory.put<char>(requestAddress + NameOffset + NameSize, name[0]);
    } catch (...) {
        try {
            memory.put<Address>(requestAddress + OwnerOffset, layout.descriptor);
            memory.write(requestAddress + NameOffset + NameSize,
                request.data() + NameOffset + NameSize, NameSize);
        } catch (...) {}
        throw;
    }
    ++publications;
    return {Outcome::added, partner + " (idle native state 0." +
        std::to_string(unsigned(request[0x21])) + ")"};
}

struct Leftover { uint64_t since = 0, fingerprint = 0; };
inline std::string tidy(const Memory& memory, const Layout& layout, Address page,
    const std::string& zone, Leftover& state, uint64_t now) {
    const Address requestAddress = layout.requests + ExtraSlot * Stride;
    const auto request = memory.get<Request>(requestAddress);
    const Address owner = field<Address>(request, OwnerOffset);
    const bool ownName = nameAt(request, 1) == zone, ownDescriptor = owner == privateDescriptor(page);
    if (request[NameOffset] != 0 || !idle(request) || !(ownName || ownDescriptor) ||
        (owner != 0 && !ownDescriptor) || request[NameOffset + 2 * NameSize] != 0) {
        state = {};
        return {};
    }
    const auto fingerprint = requestFingerprint(request, memory.get<Address>(layout.nameCell));
    if (!state.since || now < state.since || state.fingerprint != fingerprint) {
        state.since = now;
        state.fingerprint = fingerprint;
        return {};
    }
    if (now - state.since < SettleMs) return {};
    state = {};
    if (ownName) {
        const std::array<char, NameSize> empty{};
        memory.write(requestAddress + NameOffset + NameSize, empty.data(), empty.size());
    }
    if (ownDescriptor) memory.put<Address>(requestAddress + OwnerOffset, 0);
    return "Cleared only this package's settled, idle leftovers after native unloading.";
}

struct Hook {
    std::mutex mutex;
    Layout layout{};
    Address page = 0;
    std::string zone;
    WriteFn original = nullptr;
    std::atomic<bool> armed{false};
    std::atomic<bool>* engineError = nullptr;
    std::atomic<bool>* requestActive = nullptr;
    bool allowFrontEnd = false;
    unsigned publications = 0, commits = 0, notes = 0;
    void (*log)(const std::string&) noexcept = nullptr;
};
inline Hook hook;
inline void say(const std::string& text) noexcept {
    if (hook.log && hook.notes < 400) { ++hook.notes; hook.log(text); }
}
inline BOOL WINAPI observe(HANDLE process, LPVOID address, LPCVOID buffer, SIZE_T size, SIZE_T* written) {
    const auto forward = hook.original;
    const Address commit = hook.layout.requests + ExtraSlot * Stride + NameOffset;
    if (!hook.layout.requests || reinterpret_cast<Address>(address) != commit ||
        size != 1 || !buffer || process != GetCurrentProcess())
        return forward(process, address, buffer, size, written);
    const char first = *static_cast<const char*>(buffer);
    if (!first) return forward(process, address, buffer, size, written);
    const DWORD prior = GetLastError();
    std::lock_guard lock(hook.mutex);
    ++hook.commits;
    try {
        if (!hook.armed.load()) say("Kronos requested its pack before sharing was armed; original request preserved.");
        else if (hook.engineError && hook.engineError->load())
            say("Kronos requested its pack after an engine error; original request preserved.");
        else {
            const Memory memory{GetCurrentProcess()};
            const auto attempt = add(memory, hook.layout, hook.page, hook.zone, first,
                hook.publications, hook.allowFrontEnd);
            if (attempt.outcome == Outcome::added) {
                if (hook.requestActive) hook.requestActive->store(true);
                say("Shared publication " + std::to_string(hook.publications) + ": " +
                    hook.zone + " added to request 4 beside " + attempt.detail + ".");
            } else say("Package not added; Kronos's request preserved: " + attempt.detail + ".");
        }
    } catch (const std::exception& error) {
        say(std::string("Package not added: ") + error.what());
    } catch (...) { say("Package not added: unknown error."); }
    SetLastError(prior);
    return forward(process, address, buffer, size, written);
}
inline void install(HMODULE kronos, const Layout& layout, Address page, const std::string& zone,
    std::atomic<bool>* engineError, std::atomic<bool>* requestActive,
    void (*log)(const std::string&) noexcept, bool allowFrontEnd = false) {
    require(kronos && !hook.original, "Carry-forward sharing cannot be installed twice.");
    require(zoneName(zone), "Invalid package name for carry-forward sharing.");
    const auto cell = reinterpret_cast<void* volatile*>(reinterpret_cast<Address>(kronos) + KronosWriteImportRva);
    const Memory memory{GetCurrentProcess()};
    const auto current = memory.get<void*>(reinterpret_cast<Address>(cell));
    require(current && fatal_trace::executable(current), "Kronos's WriteProcessMemory import is not callable.");
    hook.layout = layout;
    hook.page = page;
    hook.zone = zone;
    hook.engineError = engineError;
    hook.requestActive = requestActive;
    hook.log = log;
    hook.allowFrontEnd = allowFrontEnd;
    hook.original = reinterpret_cast<WriteFn>(current);
    DWORD oldProtection = 0;
    if (!VirtualProtect(const_cast<void**>(cell), sizeof(void*), PAGE_READWRITE, &oldProtection)) {
        hook.original = nullptr;
        fail(winError("VirtualProtect"));
    }
    const auto previous = InterlockedCompareExchangePointer(cell, reinterpret_cast<void*>(&observe), current);
    DWORD ignored = 0;
    VirtualProtect(const_cast<void**>(cell), sizeof(void*), oldProtection, &ignored);
    if (previous != current) {
        hook.original = nullptr;
        fail("Kronos's WriteProcessMemory import changed during installation.");
    }
}
}
