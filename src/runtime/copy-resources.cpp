#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
struct Context { HMODULE target; HANDLE update; DWORD error = 0; unsigned count = 0; bool compare = false; };
BOOL CALLBACK language(HMODULE source, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR data) {
    auto& c = *reinterpret_cast<Context*>(data);
    auto resource = FindResourceExW(source, type, name, lang);
    DWORD size = resource ? SizeofResource(source, resource) : 0;
    auto loaded = resource ? LoadResource(source, resource) : nullptr;
    auto bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || !size) { c.error = GetLastError() ? GetLastError() : ERROR_RESOURCE_DATA_NOT_FOUND; return FALSE; }
    if (!c.compare) {
        if (!UpdateResourceW(c.update, type, name, lang, bytes, size)) { c.error = GetLastError(); return FALSE; }
    } else {
        auto other = FindResourceExW(c.target, type, name, lang);
        auto otherBytes = other ? LockResource(LoadResource(c.target, other)) : nullptr;
        if (!other || SizeofResource(c.target, other) != size || !otherBytes || std::memcmp(bytes, otherBytes, size)) {
            c.error = ERROR_INVALID_DATA; return FALSE;
        }
    }
    ++c.count;
    return TRUE;
}
BOOL CALLBACK name(HMODULE source, LPCWSTR type, LPWSTR id, LONG_PTR data) {
    return EnumResourceLanguagesW(source, type, id, language, data);
}
BOOL CALLBACK type(HMODULE source, LPWSTR id, LONG_PTR data) {
    return EnumResourceNamesW(source, id, name, data);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::puts("Usage: copy-resources original.dll relais.dll"); return 2; }
    auto source = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!source) { std::printf("Load resource source failed: %lu\n", GetLastError()); return 3; }
    Context c{};
    c.update = BeginUpdateResourceW(argv[2], TRUE);
    if (!c.update) { std::printf("Begin resource update failed: %lu\n", GetLastError()); FreeLibrary(source); return 4; }
    bool copied = EnumResourceTypesW(source, type, reinterpret_cast<LONG_PTR>(&c)) && c.count && !c.error;
    unsigned expected = c.count;
    bool ended = EndUpdateResourceW(c.update, !copied) != FALSE;
    if (!copied || !ended) { std::printf("Resource copy failed: %lu\n", c.error); FreeLibrary(source); return 5; }
    c.target = LoadLibraryExW(argv[2], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    c.compare = true; c.count = 0;
    bool matched = c.target && EnumResourceTypesW(source, type, reinterpret_cast<LONG_PTR>(&c)) && !c.error && c.count == expected;
    if (c.target) FreeLibrary(c.target);
    FreeLibrary(source);
    if (!matched) { std::printf("Resource verification failed: %lu\n", c.error); return 6; }
    std::printf("PASS: %u ressources recopiees, noms/types/langues et contenu compares octet par octet.\n", expected);
    return 0;
}
