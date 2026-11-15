#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <string>

// Derived read-only from SHA256 88a52103...234fcfb, Kronos v1.41.
// The callback saves all four arguments and tail-jumps through this data cell.
// The initializer installs that callback on engine RVA 0xA00FC60 and stores
// its trampoline in the cell. A different client must use a different profile.
namespace client_error_profile {
inline constexpr uintptr_t HookRva = 0xb8550, TailRva = 0xb8595,
    InitializerRva = 0xb99cf, CellRva = 0x6c4288;
inline constexpr std::array<unsigned char, 32> Hook{
    0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x28,0x49,0x8b,0xe9,0x49,0x8b,0xd8,0x8b,
    0xfa,0x8b,0xf1,0x4d,0x85,0xc0,0x74,0x15,0xba,0x01,0x00,0x00,0x00,0x48,0x8b,0xcb};
inline constexpr std::array<unsigned char, 25> Tail{
    0x4c,0x8b,0xcd,0x4c,0x8b,0xc3,0x8b,0xd7,0x8b,0xce,0x48,0x83,0xc4,0x28,0x5f,0x5e,
    0x5d,0x5b,0x48,0xff,0x25,0xda,0xbc,0x60,0x00};
inline constexpr std::array<unsigned char, 40> Initializer{
    0x4c,0x8d,0x0d,0x7a,0xeb,0xff,0xff,0x41,0xb8,0x08,0x00,0x00,0x00,0x48,0x8d,0x15,
    0xfd,0x51,0x0b,0x00,0xb9,0x60,0xfc,0x00,0x0a,0xe8,0xd3,0x36,0x02,0x00,0x48,0x8b,
    0xd0,0x48,0x89,0x05,0x91,0xa8,0x60,0x00};
template<class Reader> bool matches(Reader&& reader, uintptr_t base) {
    std::array<unsigned char, Hook.size()> hook{};
    std::array<unsigned char, Tail.size()> tail{};
    std::array<unsigned char, Initializer.size()> initializer{};
    return reader(base + HookRva, hook.data(), hook.size()) && hook == Hook &&
        reader(base + TailRva, tail.data(), tail.size()) && tail == Tail &&
        reader(base + InitializerRva, initializer.data(), initializer.size()) && initializer == Initializer;
}
// Failure-only, read-only evidence: three fixed ranges (97 bytes total).
// Do not scan memory, follow changed pointers, or relax matches().
template<class Reader> std::string describeMismatch(Reader&& reader, uintptr_t base) {
    std::ostringstream out;
    out << "Kronos callback profile: base=0x" << std::hex << base;
    const auto range = [&](const char* name, uintptr_t rva, const auto& expected) {
        auto actual = expected;
        actual.fill(0);
        const bool readable = reader(base + rva, actual.data(), actual.size());
        out << '\n' << name << " rva=0x" << std::hex << rva
            << " readable=" << readable;
        if (!readable) return;
        out << " matches=" << (actual == expected) << " expected=";
        const auto bytes = [&](const auto& values) {
            for (auto value : values) out << std::setw(2) << std::setfill('0') << unsigned(value);
        };
        bytes(expected); out << " actual="; bytes(actual);
    };
    range("hook", HookRva, Hook);
    range("tail", TailRva, Tail);
    range("initializer", InitializerRva, Initializer);
    return out.str();
}
}
