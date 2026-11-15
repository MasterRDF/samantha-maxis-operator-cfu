#include "ops_patch.h"
#include "native_template.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace mwcfops {
namespace {
struct Site {
    uint32_t address;
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    DWORD protection = 0;
};
size_t Align(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}
bool Relative(uint8_t* field, uintptr_t next, uintptr_t target) {
    const int64_t distance = static_cast<int64_t>(target) - static_cast<int64_t>(next);
    if (distance < INT32_MIN || distance > INT32_MAX) return false;
    const int32_t displacement = static_cast<int32_t>(distance);
    memcpy(field, &displacement, sizeof(displacement));
    return true;
}
uint8_t* AllocateNear(uint8_t* image, size_t bytes) {
    SYSTEM_INFO information{};
    GetSystemInfo(&information);
    const uintptr_t granularity = information.dwAllocationGranularity;
    const uintptr_t origin = reinterpret_cast<uintptr_t>(image) & ~(granularity - 1);
    for (uintptr_t distance = granularity; distance < 0x70000000; distance += granularity) {
        for (const bool above : {true, false}) {
            if (!above && distance >= origin) continue;
            const uintptr_t address = above ? origin + distance : origin - distance;
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory))) continue;
            if (memory.State != MEM_FREE || memory.RegionSize < bytes) continue;
            void* allocation = VirtualAlloc(reinterpret_cast<void*>(address), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (allocation) return static_cast<uint8_t*>(allocation);
        }
    }
    return nullptr;
}
}

bool InstallStable(uint8_t* image, size_t imageBytes, const Input& input, Result& result, std::string& error, Fault fault) {
    result = {};
    auto fail = [&](const char* message) { error = message; return false; };
    if (!image || imageBytes != kStockImageBytes) return fail("unsupported image size");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > imageBytes) return fail("invalid image header");
    auto* headers = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE || headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        headers->FileHeader.TimeDateStamp != kStockTimestamp || headers->FileHeader.NumberOfSections != kStockSections ||
        headers->OptionalHeader.SizeOfImage != imageBytes) return fail("unsupported Kronos build");
    if (input.operators.size() != 128 || input.skins.size() != 661) return fail("operator manifest counts differ from scripts");
    if (*reinterpret_cast<uint32_t*>(image + 0x2b3874) != 0xffffffff) return fail("operator slots already initialized");
    if (memcmp(image + 0x172510, image + 0x2b4630, 72 * 8) || memcmp(image + 0x172750, image + 0x2b38a0, 434 * 8)) return fail("operator tables already sorted or changed");
    const std::array<std::pair<uint32_t, std::vector<uint8_t>>, 6> guards{{
        {0xb8550, {0x40,0x53,0x55,0x56,0x57}}, {0xb8555, {0x48,0x83,0xec,0x28}},
        {0xb8590, {0xe8,0x0b,0xfc,0xff,0xff}}, {0xb8595, {0x4c,0x8b,0xcd,0x4c,0x8b,0xc3}},
        {0xb859b, {0x8b,0xd7,0x8b,0xce,0x48,0x83,0xc4,0x28,0x5f,0x5e,0x5d,0x5b,0x48,0xff,0x25,0xda,0xbc,0x60,0x00}},
        {0xe65a0, {0x85,0xdb,0x75,0x24,0x85,0xf6,0x7e,0x20,0xc6,0x05}}
    }};
    for (const auto& guard : guards) if (memcmp(image + guard.first, guard.second.data(), guard.second.size())) return fail("native guard site mismatch");
    const int32_t rebuildOffset = *reinterpret_cast<int32_t*>(image + 0xe65aa);
    if (0xe65a8 + 7 + rebuildOffset != 0x768c18) return fail("skin rebuild flag target mismatch");
    std::vector<uint64_t> operators(reinterpret_cast<uint64_t*>(image + 0x172510), reinterpret_cast<uint64_t*>(image + 0x172510) + 72);
    std::vector<uint64_t> skins(reinterpret_cast<uint64_t*>(image + 0x172750), reinterpret_cast<uint64_t*>(image + 0x172750) + 434);
    operators.insert(operators.end(), input.operators.begin(), input.operators.end());
    skins.insert(skins.end(), input.skins.begin(), input.skins.end());
    if (std::set<uint64_t>(operators.begin(), operators.end()).size() != operators.size() || std::set<uint64_t>(skins.begin(), skins.end()).size() != skins.size()) return fail("duplicate operator or skin hashes");
    for (const auto value : operators) if (!value || value >> 63) return fail("invalid operator hash");
    for (const auto value : skins) if (!value || value >> 63) return fail("invalid skin hash");
    const size_t operatorBytes = operators.size() * 8;
    const size_t skinBytes = skins.size() * 8;
    const size_t readOperators = 0;
    const size_t readSkins = Align(operatorBytes, 16);
    const size_t writeSkins = Align(readSkins + skinBytes, 16);
    const size_t writeOperators = Align(writeSkins + skinBytes, 16);
    const size_t slots = Align(writeOperators + operatorBytes, 16);
    const size_t counter = Align(slots + 256 * 8, 16);
    const size_t dataBytes = Align(counter + 16, 4096);
    const size_t allocationBytes = dataBytes + 4096;
    if (fault == Fault::Allocation) return fail("injected allocation failure");
    uint8_t* allocation = AllocateNear(image, allocationBytes);
    if (!allocation) return fail("cannot allocate nearby operator tables");
    auto discard = [&](const char* message) { VirtualFree(allocation, 0, MEM_RELEASE); return fail(message); };
    memcpy(allocation + readOperators, operators.data(), operatorBytes);
    memcpy(allocation + writeOperators, operators.data(), operatorBytes);
    memcpy(allocation + readSkins, skins.data(), skinBytes);
    memcpy(allocation + writeSkins, skins.data(), skinBytes);
    uint8_t* code = allocation + dataBytes;
    memcpy(code, kGuardTemplate, sizeof(kGuardTemplate));
    for (const auto& relocation : kGuardRelocations) {
        const uintptr_t target = relocation.counter ? reinterpret_cast<uintptr_t>(allocation + counter) : reinterpret_cast<uintptr_t>(image + relocation.target);
        if (!Relative(code + relocation.offset, reinterpret_cast<uintptr_t>(code + relocation.next), target)) return discard("guard relocation out of reach");
    }
    struct Lea { uint32_t address; uint32_t oldTarget; size_t destination; uint8_t opcode[3]; };
    const Lea references[] = {
        {0xe4eca,0x172510,readOperators,{0x48,0x8d,0x1d}},{0xe4ed1,0x172750,readOperators+operatorBytes,{0x4c,0x8d,0x25}},
        {0xe4ed8,0x780580,slots,{0x4c,0x8d,0x2d}},{0xe5034,0x780580,slots,{0x48,0x8d,0xd}},{0xe5051,0x780580,slots,{0x48,0x8d,0x5}},
        {0xe54d3,0x780580,slots,{0x48,0x8d,0xd}},{0xe66cf,0x780580,slots,{0x4c,0x8d,0x35}},{0xe6280,0x172510,readOperators,{0x48,0x8d,0x5}},
        {0xe6526,0x172750,readOperators+operatorBytes,{0x48,0x8d,0x15}},{0xe600b,0x172750,readSkins,{0x48,0x8d,0x15}},
        {0xe5f6b,0x2b38a0,writeSkins,{0x48,0x8d,0x3d}},{0xe5f77,0x2b4630,writeSkins+skinBytes,{0x4c,0x8d,0x35}},
        {0xe57f9,0x2b4870,writeOperators+operatorBytes,{0x48,0x8d,0x2d}},{0xe5800,0x2b4630,writeOperators,{0x48,0x8d,0x3d}}
    };
    std::vector<Site> sites;
    for (const auto& reference : references) {
        uint8_t* instruction = image + reference.address;
        if (memcmp(instruction, reference.opcode, 3) != 0) return discard("LEA opcode mismatch");
        if (static_cast<int64_t>(reference.address) + 7 + *reinterpret_cast<int32_t*>(instruction + 3) != reference.oldTarget) return discard("LEA original target mismatch");
        Site site{reference.address, {instruction, instruction + 7}, {instruction, instruction + 7}};
        if (!Relative(site.after.data() + 3, reinterpret_cast<uintptr_t>(instruction + 7), reinterpret_cast<uintptr_t>(allocation + reference.destination))) return discard("table reference out of reach");
        sites.push_back(std::move(site));
    }
    struct Immediate { uint32_t address; uint32_t before; uint32_t after; bool extended; };
    const Immediate immediates[] = {{0xe57f4,72,200,false},{0xe5f72,434,1095,false},{0xe6012,434*8,1095*8,true},{0xe4f3f,72,200,true}};
    for (const auto& immediate : immediates) {
        uint8_t* instruction = image + immediate.address;
        const size_t prefix = immediate.extended ? 2 : 1;
        if ((immediate.extended && (instruction[0] != 0x41 || instruction[1] != 0xb8)) || (!immediate.extended && instruction[0] != 0xbb)) return discard("count opcode mismatch");
        if (*reinterpret_cast<uint32_t*>(instruction + prefix) != immediate.before) return discard("count value mismatch");
        Site site{immediate.address, {instruction, instruction + prefix + 4}, {instruction, instruction + prefix + 4}};
        memcpy(site.after.data() + prefix, &immediate.after, 4);
        sites.push_back(std::move(site));
    }
    Site hook{0xb8595, {image+0xb8595,image+0xb859b}, {0xe9,0,0,0,0,0x90}};
    if (!Relative(hook.after.data()+1, reinterpret_cast<uintptr_t>(image+0xb859a), reinterpret_cast<uintptr_t>(code))) return discard("guard jump out of reach");
    sites.push_back(std::move(hook));
    sites.push_back(Site{0xe65a2,{0x75,0x24},{0x90,0x90}});
    DWORD previous = 0;
    if (fault == Fault::CodeProtection || !VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous)) return discard("cannot make guard executable");
    size_t writable = 0;
    for (auto& site : sites) {
        if ((fault == Fault::ImageProtection && writable == 3) || !VirtualProtect(image + site.address, site.after.size(), PAGE_EXECUTE_READWRITE, &site.protection)) {
            for (size_t index = writable; index > 0; index--) {
                DWORD ignored = 0;
                VirtualProtect(image + sites[index-1].address, sites[index-1].after.size(), sites[index-1].protection, &ignored);
            }
            return discard("cannot prepare all native patch pages");
        }
        writable++;
    }
    bool unchanged = *reinterpret_cast<uint32_t*>(image + 0x2b3874) == 0xffffffff &&
        !memcmp(image + 0x172510, image + 0x2b4630, 72 * 8) && !memcmp(image + 0x172750, image + 0x2b38a0, 434 * 8);
    for (const auto& site : sites) if (memcmp(image + site.address, site.before.data(), site.before.size())) unchanged = false;
    if (!unchanged) {
        for (size_t index = sites.size(); index > 0; index--) {
            DWORD ignored = 0;
            VirtualProtect(image + sites[index-1].address, sites[index-1].before.size(), sites[index-1].protection, &ignored);
        }
        return discard("Kronos changed during patch preparation");
    }
    for (const auto& site : sites) memcpy(image + site.address, site.after.data(), site.after.size());
    FlushInstructionCache(GetCurrentProcess(), image, imageBytes);
    FlushInstructionCache(GetCurrentProcess(), code, sizeof(kGuardTemplate));
    bool protectionRestored = true;
    for (size_t index = sites.size(); index > 0; index--) {
        DWORD ignored = 0;
        auto& site = sites[index-1];
        if (!VirtualProtect(image + site.address, site.after.size(), site.protection, &ignored)) protectionRestored = false;
    }
    result = {allocation, allocationBytes, dataBytes, code, reinterpret_cast<LONG*>(allocation + counter)};
    error = protectionRestored ? "" : "patch active, but one native page protection could not be restored";
    return true;
}
bool InstallDev(uint8_t* image, size_t imageBytes, const Input& input, Result& result, std::string& error, Fault fault) {
    result = {};
    auto fail = [&](const char* message) { error = message; return false; };
    if (!image || imageBytes != kDevImageBytes) return fail("unsupported image size");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > imageBytes) return fail("invalid image header");
    auto* headers = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE || headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        headers->FileHeader.TimeDateStamp != kDevTimestamp || headers->FileHeader.NumberOfSections != kDevSections ||
        headers->OptionalHeader.SizeOfImage != imageBytes) return fail("unsupported Kronos build");
    if (input.operators.size() != 128 || input.skins.size() != 661) return fail("operator manifest counts differ from scripts");
    if (*reinterpret_cast<uint32_t*>(image + 0x311c24) != 0xffffffff) return fail("operator slots already initialized");
    if (memcmp(image + 0x1c1e60, image + 0x3129c0, 72 * 8) || memcmp(image + 0x1c20a0, image + 0x311c30, 434 * 8)) return fail("operator tables already sorted or changed");
    std::vector<uint64_t> operators(reinterpret_cast<uint64_t*>(image + 0x1c1e60), reinterpret_cast<uint64_t*>(image + 0x1c1e60) + 72);
    std::vector<uint64_t> skins(reinterpret_cast<uint64_t*>(image + 0x1c20a0), reinterpret_cast<uint64_t*>(image + 0x1c20a0) + 434);
    operators.insert(operators.end(), input.operators.begin(), input.operators.end());
    skins.insert(skins.end(), input.skins.begin(), input.skins.end());
    if (std::set<uint64_t>(operators.begin(), operators.end()).size() != operators.size() || std::set<uint64_t>(skins.begin(), skins.end()).size() != skins.size()) return fail("duplicate operator or skin hashes");
    for (const auto value : operators) if (!value || value >> 63) return fail("invalid operator hash");
    for (const auto value : skins) if (!value || value >> 63) return fail("invalid skin hash");
    const size_t operatorBytes = operators.size() * 8;
    const size_t skinBytes = skins.size() * 8;
    const size_t readOperators = 0;
    const size_t readSkins = Align(operatorBytes, 16);
    const size_t writeSkins = Align(readSkins + skinBytes, 16);
    const size_t writeOperators = Align(writeSkins + skinBytes, 16);
    const size_t slots = Align(writeOperators + operatorBytes, 16);
    const size_t counter = Align(slots + 256 * 8, 16);
    const size_t dataBytes = Align(counter + 16, 4096);
    const size_t allocationBytes = dataBytes + 4096;
    if (fault == Fault::Allocation) return fail("injected allocation failure");
    uint8_t* allocation = AllocateNear(image, allocationBytes);
    if (!allocation) return fail("cannot allocate nearby operator tables");
    auto discard = [&](const char* message) { VirtualFree(allocation, 0, MEM_RELEASE); return fail(message); };
    memcpy(allocation + readOperators, operators.data(), operatorBytes);
    memcpy(allocation + writeOperators, operators.data(), operatorBytes);
    memcpy(allocation + readSkins, skins.data(), skinBytes);
    memcpy(allocation + writeSkins, skins.data(), skinBytes);
    uint8_t* code = allocation + dataBytes;
    struct Lea { uint32_t address; uint32_t oldTarget; size_t destination; uint8_t opcode[3]; };
    const Lea references[] = {
        {0x1128da,0x1c1e60,readOperators,{0x48,0x8d,0x1d}},{0x1128e4,0x1c20a0,readOperators+operatorBytes,{0x4c,0x8d,0x25}},
        {0x11288f,0xb61b30,slots,{0x4c,0x8d,0x35}},{0x11296c,0xb61b30,slots,{0x4c,0x8d,0x35}},{0x112a2f,0xb61b30,slots,{0x48,0x8d,0x5}},
        {0x112e39,0xb61b30,slots,{0x48,0x8d,0xd}},{0x114181,0xb61b30,slots,{0x4c,0x8d,0x35}},{0x113d1c,0x1c1e60,readOperators,{0x48,0x8d,0x5}},
        {0x113ffc,0x1c20a0,readOperators+operatorBytes,{0x48,0x8d,0x15}},{0x113a92,0x1c20a0,readSkins,{0x48,0x8d,0x15}},
        {0x1139ea,0x311c30,writeSkins,{0x48,0x8d,0x3d}},{0x1139f6,0x3129c0,writeSkins+skinBytes,{0x4c,0x8d,0x35}},
        {0x113264,0x312c00,writeOperators+operatorBytes,{0x48,0x8d,0x2d}},{0x11326b,0x3129c0,writeOperators,{0x48,0x8d,0x3d}}
    };
    std::vector<Site> sites;
    for (const auto& reference : references) {
        uint8_t* instruction = image + reference.address;
        if (memcmp(instruction, reference.opcode, 3) != 0) return discard("LEA opcode mismatch");
        if (static_cast<int64_t>(reference.address) + 7 + *reinterpret_cast<int32_t*>(instruction + 3) != reference.oldTarget) return discard("LEA original target mismatch");
        Site site{reference.address, {instruction, instruction + 7}, {instruction, instruction + 7}};
        if (!Relative(site.after.data() + 3, reinterpret_cast<uintptr_t>(instruction + 7), reinterpret_cast<uintptr_t>(allocation + reference.destination))) return discard("table reference out of reach");
        sites.push_back(std::move(site));
    }
    struct Immediate { uint32_t address; uint32_t before; uint32_t after; bool extended; };
    const Immediate immediates[] = {{0x11325f,72,200,false},{0x1139f1,434,1095,false},{0x113a99,434*8,1095*8,true},{0x112942,72,200,true}};
    for (const auto& immediate : immediates) {
        uint8_t* instruction = image + immediate.address;
        const size_t prefix = immediate.extended ? 2 : 1;
        if ((immediate.extended && (instruction[0] != 0x41 || instruction[1] != 0xb8)) || (!immediate.extended && instruction[0] != 0xbb)) return discard("count opcode mismatch");
        if (*reinterpret_cast<uint32_t*>(instruction + prefix) != immediate.before) return discard("count value mismatch");
        Site site{immediate.address, {instruction, instruction + prefix + 4}, {instruction, instruction + prefix + 4}};
        memcpy(site.after.data() + prefix, &immediate.after, 4);
        sites.push_back(std::move(site));
    }
    DWORD previous = 0;
    if (fault == Fault::CodeProtection || !VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous)) return discard("cannot make guard executable");
    size_t writable = 0;
    for (auto& site : sites) {
        if ((fault == Fault::ImageProtection && writable == 3) || !VirtualProtect(image + site.address, site.after.size(), PAGE_EXECUTE_READWRITE, &site.protection)) {
            for (size_t index = writable; index > 0; index--) {
                DWORD ignored = 0;
                VirtualProtect(image + sites[index-1].address, sites[index-1].after.size(), sites[index-1].protection, &ignored);
            }
            return discard("cannot prepare all native patch pages");
        }
        writable++;
    }
    bool unchanged = *reinterpret_cast<uint32_t*>(image + 0x311c24) == 0xffffffff &&
        !memcmp(image + 0x1c1e60, image + 0x3129c0, 72 * 8) && !memcmp(image + 0x1c20a0, image + 0x311c30, 434 * 8);
    for (const auto& site : sites) if (memcmp(image + site.address, site.before.data(), site.before.size())) unchanged = false;
    if (!unchanged) {
        for (size_t index = sites.size(); index > 0; index--) {
            DWORD ignored = 0;
            VirtualProtect(image + sites[index-1].address, sites[index-1].before.size(), sites[index-1].protection, &ignored);
        }
        return discard("Kronos changed during patch preparation");
    }
    for (const auto& site : sites) memcpy(image + site.address, site.after.data(), site.after.size());
    FlushInstructionCache(GetCurrentProcess(), image, imageBytes);
    
    bool protectionRestored = true;
    for (size_t index = sites.size(); index > 0; index--) {
        DWORD ignored = 0;
        auto& site = sites[index-1];
        if (!VirtualProtect(image + site.address, site.after.size(), site.protection, &ignored)) protectionRestored = false;
    }
    result = {allocation, allocationBytes, dataBytes, code, reinterpret_cast<LONG*>(allocation + counter)};
    error = protectionRestored ? "" : "patch active, but one native page protection could not be restored";
    return true;
}
bool Install(uint8_t* image, size_t bytes, const Input& input, Result& result, std::string& error, Fault fault) {
    if (bytes == kDevImageBytes) return InstallDev(image, bytes, input, result, error, fault);
    return InstallStable(image, bytes, input, result, error, fault);
}
} // namespace mwcfops
