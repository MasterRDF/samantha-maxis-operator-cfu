#pragma once
#include "fatal-trace.hpp"
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <bcrypt.h>

// Trust policy for the five user-installed mod files. This does not produce
// publisher signatures; it admits exact, hash-pinned files in this client.
namespace trusted_fastfiles {
namespace fs = std::filesystem;
using Parser = bool (__fastcall*)(void*, const char*, bool*, void*);
using HashFile = std::string (*)(const fs::path&);
using StreamVerifier = bool (__fastcall*)(const void*, const char*);
struct Entry { std::string name,sha; bool server; std::string streamSha; };
inline std::array<Entry,5> entries{};
inline std::atomic<Parser> original{nullptr};
inline std::atomic<StreamVerifier> originalStream{nullptr};
inline std::atomic<unsigned> parseCount{0};
inline HashFile hashFile = nullptr;
inline fs::path root;
inline std::mutex logMutex;

inline void note(const std::string& message) noexcept {
    try {
        std::lock_guard lock(logMutex);
        std::ofstream log(root / L"kronos" / L"Logs" / L"bo7-runtime-import.log", std::ios::app);
        if (log) log << message << '\n';
    } catch (...) {}
}
inline std::string readName(const char* filename) {
    if (!filename) return {};
    std::string name; name.reserve(96);
    for (unsigned i = 0; i < 256; ++i) {
        char c = 0; SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), filename + i, &c, 1, &read) || read != 1) return {};
        if (!c) return name;
        name.push_back(c);
    }
    return {};
}
inline const Entry* matchName(std::string_view name) {
    if (name.empty()) return nullptr;
    if (!name.empty() && (name.front() == '\\' || name.front() == '/')) name.remove_prefix(1);
    std::string normalized(name);
    const fs::path path(normalized);
    if (path.is_absolute()) {
        if (_wcsicmp(path.parent_path().lexically_normal().c_str(), root.lexically_normal().c_str()) != 0) return nullptr;
        normalized = path.filename().string();
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c); });
    for (const auto& entry : entries) {
        std::string_view expected(entry.name);
        if (normalized == expected || normalized == expected.substr(0, expected.size() - 3)) return &entry;
    }
    return nullptr;
}
inline const Entry* match(const char* filename) { return matchName(readName(filename)); }
inline std::string hashMemory(const void* memory, size_t size) {
    std::vector<unsigned char> bytes(size); SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), memory, bytes.data(), bytes.size(), &read) || read != size) return {};
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        ~Handles() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD objectSize = 0, result = 0;
    if (BCryptGetProperty(handles.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof objectSize, &result, 0) < 0) return {};
    std::vector<unsigned char> object(objectSize);
    if (BCryptCreateHash(handles.algorithm, &handles.hash, object.data(), objectSize, nullptr, 0, 0) < 0 ||
        BCryptHashData(handles.hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0) return {};
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(handles.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
    std::ostringstream out; for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}
inline bool headerMatches(const void* header, bool server) {
    std::array<uint32_t, 5> values{}; SIZE_T read = 0;
    if (!header || !ReadProcessMemory(GetCurrentProcess(), header, values.data(), sizeof values, &read) || read != sizeof values) return false;
    constexpr std::array<uint32_t, 4> client{0x3579fe1b,0x629e4fd7,0x5199af3f,0x9777c8fc};
    constexpr std::array<uint32_t, 4> dedicated{0xac37db14,0xc38fb1de,0xeed17cb6,0xb74bdfee};
    const auto& wanted = server ? dedicated : client;
    return (values[0] & 0xff) == 1 && std::equal(wanted.begin(), wanted.end(), values.begin() + 1);
}
inline bool admit(const Entry& entry, const void* header, bool* authenticated) noexcept {
    try {
        if (!authenticated || !fatal_trace::writable(authenticated, sizeof(bool)) || !headerMatches(header, entry.server)) {
            note(std::string("MOD REFUSED: unexpected header: ") + entry.name); return false;
        }
        if (!hashFile || hashFile(root / entry.name) != entry.sha) {
            note(std::string("MOD REFUSED: checksum mismatch: ") + entry.name); return false;
        }
        *authenticated = true;
        note(std::string("MOD ACCEPTED BY CHECKSUM: ") + entry.name + "; publisher signature not recreated; assets not confirmed.");
        return true;
    } catch (...) { note(std::string("MOD REFUSED: verification failed: ") + entry.name); return false; }
}
__declspec(noinline) inline bool __fastcall parse(void* header, const char* filename, bool* authenticated, void* reader) {
    const auto name = readName(filename);
    const bool parsed = original.load(std::memory_order_acquire)(header, filename, authenticated, reader);
    bool already = false; SIZE_T read = 0;
    const bool readable = authenticated && ReadProcessMemory(GetCurrentProcess(), authenticated, &already, sizeof already, &read) && read == sizeof already;
    if (parseCount.fetch_add(1) < 16 || matchName(name) != nullptr)
        note("PARSE TAFA: [" + name + "] parsed=" + std::to_string(parsed) + " publisher=" + (readable ? std::to_string(already) : "unreadable"));
    if (!parsed) return parsed;
    if (const auto entry = matchName(name)) {
        if (readable && !already)
            admit(*entry, header, authenticated);
    }
    return parsed;
}
__declspec(noinline) inline bool __fastcall verifyStream(const void* header, const char* filename) {
    const bool verified = originalStream.load(std::memory_order_acquire)(header, filename);
    if (verified) return verified;
    try {
        const auto entry = match(filename);
        if (!entry) return false;
        if (!hashFile || hashFile(root / entry->name) != entry->sha || hashMemory(header, 0x1970) != entry->streamSha) {
            note(std::string("MOD STREAM REFUSED: checksum mismatch: ") + entry->name); return false;
        }
        note(std::string("FLUX MOD ACCEPTED BY CHECKSUMS: ") + entry->name + "; publisher signature not recreated.");
        return true;
    } catch (...) { return false; }
}
}
