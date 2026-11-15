#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <bcrypt.h>
#include <array>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <mutex>
#include "request-lifecycle.hpp"


namespace fs = std::filesystem;
using Address = uintptr_t;
constexpr size_t Stride = 0x1b8, RequestCount = 11, ExtraSlot = 4;
constexpr Address RequestsRva = 0x13197200, DescriptorRva = 0x0db43e90;
constexpr Address NameCellRva = 0x131971b8;
constexpr uint64_t DescriptorHash = 0x36548c03211b36a1;
inline std::string ZoneName = "runtime_smoke";
constexpr char ClientSha[] = "88a52103c5555b2760f1e7ad13a594e8ef0440f007b45a10bddc61dda234fcfb";
constexpr char GameSha[] = "b0b12b0e7ec25ce855ea6e9d042fc8f390b8d48097cb10e0899e8334f35b6db8";

constexpr char GlobalSha[] = "d47c0b7d129ca21678563a7e1ac3620c1edfe8823415f0e8e9d3500b3ae418ec";
constexpr char FrenchSha[] = "dbf9f71a6c9a2738d8013fbcedeb30ff32e0588dde0676cb3361971575ec0c7d";

struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }
std::string winError(const char* operation) {
    return std::string(operation) + ": Windows error " + std::to_string(GetLastError());
}
struct Memory {
    HANDLE h;
    void read(Address from, void* into, size_t size) const {
        SIZE_T done = 0;
        if (!ReadProcessMemory(h, reinterpret_cast<void*>(from), into, size, &done)) fail(winError("ReadProcessMemory"));
        require(done == size, "ReadProcessMemory : incomplete read.");
    }
    void write(Address to, const void* from, size_t size) const {
        SIZE_T done = 0;
        if (!WriteProcessMemory(h, reinterpret_cast<void*>(to), from, size, &done)) fail(winError("WriteProcessMemory"));
        require(done == size, "WriteProcessMemory : incomplete write.");
    }
    template<class T> T get(Address a) const { T value{}; read(a, &value, sizeof value); return value; }
    template<class T> void put(Address a, const T& v) const { write(a, &v, sizeof v); }
};
struct Layout { Address requests, descriptor, nameCell; };
using Request = std::array<unsigned char, Stride>;
using Table = std::array<Request, RequestCount>;
template<class T> T field(const Request& r, size_t offset) {
    T v{}; std::memcpy(&v, r.data() + offset, sizeof v); return v;
}
template<class T> void setField(Request& r, size_t offset, T value) {
    std::memcpy(r.data() + offset, &value, sizeof value);
}
std::string nameAt(const Request& r, size_t index) {
    auto p = reinterpret_cast<const char*>(r.data() + 0xea + index * 0x40);
    return std::string(p, strnlen_s(p, 0x40));
}
Table snapshot(const Memory& m, const Layout& l) { return m.get<Table>(l.requests); }
bool freeSlot(const Request& r) {
    return field<Address>(r, 0x1b0) == 0 &&
        std::all_of(r.begin() + 0xea, r.begin() + 0x1aa, [](auto v) { return v == 0; });
}
bool idleOwnedSlot(const Request& r,Address descriptor) {
    const Address owner=field<Address>(r,0x1b0);
    // Native names are bounded C strings. Clearing the leading NUL makes a
    // name empty even when bytes from its previous value remain after it.
    // Initial publication still requires freeSlot()'s zero-filled storage.
    return (owner==0||owner==descriptor)&&r[0x20]==0&&(r[0x21]==0||r[0x21]==3)&&
        r[0xea]==0&&r[0x12a]==0&&r[0x16a]==0;
}
uint64_t requestFingerprint(const Request& request,Address cell) {
    uint64_t hash=0xcbf29ce484222325;
    for(unsigned char b:request)hash=(hash^b)*0x100000001b3;
    for(size_t i=0;i<sizeof cell;i++)hash=(hash^static_cast<unsigned char>(cell>>(8*i)))*0x100000001b3;
    return hash;
}
bool zombieReady(const Table& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        if (i == ExtraSlot || t[i][0x20] != 2 || field<Address>(t[i], 0x1b0) == 0) continue;
        for (size_t n = 0; n < 3; ++n) if (nameAt(t[i], n) == "ingame_zm") return true;
    }
    return false;
}
void validateDescriptor(const Memory& m, const Layout& l) {
    require(m.get<uint64_t>(l.descriptor) == DescriptorHash, "Unknown engine descriptor hash.");
    require(m.get<uint32_t>(l.descriptor + 8) == 20, "Unknown engine descriptor type.");
    require(m.get<uint32_t>(l.descriptor + 12) == 1, "Unknown engine descriptor name count.");
    require(m.get<Address>(l.descriptor + 16) == l.nameCell, "Unexpected engine name table.");
}
void preconditions(const Memory& m, const Layout& l,Address ownPage=0,bool republishing=false) {
    validateDescriptor(m, l);
    const Address cell=m.get<Address>(l.nameCell);
    require(cell==0||(ownPage!=0&&cell==ownPage), "Another owner is using the extra package slot.");
    auto t = snapshot(m, l);
    require(zombieReady(t), "The ingame_zm request is not in state 2.");
    require(idleOwnedSlot(t[ExtraSlot],l.descriptor)&&(republishing||freeSlot(t[ExtraSlot])), "Request 4 is busy or transitioning; no publication.");
}
struct Installation {
    Address allocation = 0;
    Request original{};
    bool committed = false;
    Address originalNameCell = 0;
    bool touched = false;
    bool republishing = false;
};
// Same data fields and publication order as the inspected Kronos routine at RVA 0x85b29.
// No executable page is changed and no remote thread is created.
void publish(const Memory& m, const Layout& l, Installation& in) {
    preconditions(m, l,in.republishing?in.allocation:0,in.republishing);
    in.original = m.get<Request>(l.requests + ExtraSlot * Stride);
    in.originalNameCell=m.get<Address>(l.nameCell);
    Address q = l.requests + ExtraSlot * Stride;
    const auto latest=m.get<Request>(q);
    require(m.get<Address>(l.nameCell)==in.originalNameCell&&
        (in.originalNameCell==0||(in.republishing&&in.originalNameCell==in.allocation))&&
        idleOwnedSlot(latest,l.descriptor)&&(in.republishing||freeSlot(latest)), "The request changed before publication.");
    in.touched=true;
    m.put<Address>(l.nameCell, in.allocation);
    m.put<Address>(q + 0x1b0, l.descriptor);
    m.put<uint32_t>(q + 0x24, 0);
    m.write(q + 0xeb, ZoneName.c_str() + 1, ZoneName.size());
    m.put<unsigned char>(q + 0xea, ZoneName[0]);
    in.committed = true;
}
// Only used for a failed, unpublished transaction and in the isolated self-test.
// A published live request is left valid until the game exits (asynchronous engine).
void rollbackUnpublished(const Memory& m, const Layout& l, const Installation& in) {
    require(!in.committed, "A published asynchronous request cannot be rolled back.");
    if(!in.touched)return;
    Address q = l.requests + ExtraSlot * Stride;
    if (m.get<Address>(l.nameCell) != in.allocation) return;
    auto now = m.get<Request>(q);
    require(field<Address>(now, 0x1b0) == l.descriptor || field<Address>(now, 0x1b0) == 0,
            "Request ownership changed; rollback stopped.");
    require(nameAt(now, 0).empty(), "The request is already visible; rollback stopped.");
    m.put<Address>(q + 0x1b0, field<Address>(in.original, 0x1b0));
    m.write(q + 0xea, in.original.data() + 0xea, 0xc0);
    m.put<uint32_t>(q + 0x24, field<uint32_t>(in.original, 0x24));
    m.put<Address>(l.nameCell, in.originalNameCell);
}
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "File missing or unreadable: " + path.filename().string());
    BCRYPT_ALG_HANDLE alg = nullptr;
    require(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "BCryptOpenAlgorithmProvider.");
    DWORD objectSize = 0, result = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof objectSize, &result, 0);
    std::vector<unsigned char> object(objectSize);
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto status = BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0);
    if (status < 0) { BCryptCloseAlgorithmProvider(alg, 0); fail("BCryptCreateHash."); }
    std::vector<unsigned char> buffer(1024 * 1024);
    bool ok = true;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        auto count = input.gcount();
        if (count && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) < 0) { ok = false; break; }
    }
    std::array<unsigned char, 32> digest{};
    ok = ok && input.eof() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    require(ok, "SHA256 calculation failed.");
    std::ostringstream out;
    for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}
