// Stand-in for bo7-runtime.dll in the starter's offline test: records that Bo7RuntimeAttach was called and
// whether the game's own XINPUT9_1_0.dll was loaded at that moment.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
extern "C" __declspec(dllexport) DWORD WINAPI Bo7RuntimeAttach(void *) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return 1;
    std::wstring path = exe;
    path = path.substr(0, path.rfind(L'\\')) + L"\\runtime-attached.txt";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 2;
    const char *text = GetModuleHandleW(L"XINPUT9_1_0.dll") ? "PASS" : "NO-KRONOS";
    DWORD written;
    WriteFile(f, text, DWORD(strlen(text)), &written, nullptr);
    CloseHandle(f);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, void *) { return TRUE; }
