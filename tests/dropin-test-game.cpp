// Stand-in for cod25-cod.exe in the starter's offline test. Like the game it imports TBS.DLL and
// XINPUT9_1_0.dll statically. It checks that a tbs call still reaches the system's tbs.dll through the
// starter, then waits for the starter to call the stand-in runtime.
// Exit codes: 0 started, 1 not started (expected in the "nothing to start" layouts), 2+ a failure.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tbs.h>
#include <cstdio>
#include <string>

extern "C" __declspec(dllimport) DWORD WINAPI XInputGetState(DWORD user, void *output);

int wmain() {
    DWORD state = 0;
    if (XInputGetState(7, &state) != 11 || state != 7) return 2;
    wchar_t own[MAX_PATH], loaded[MAX_PATH];
    GetModuleFileNameW(nullptr, own, MAX_PATH);
    std::wstring dir = own;
    dir = dir.substr(0, dir.rfind(L'\\'));
    HMODULE local = GetModuleHandleW(L"tbs.dll");
    if (!local || !GetModuleFileNameW(local, loaded, MAX_PATH) || _wcsicmp(loaded, (dir + L"\\tbs.dll").c_str()) != 0) {
        std::puts("the folder's tbs.dll was not the one loaded");
        return 3;
    }
    // The same call through the import (the starter's forwarder) and straight from the system copy.
    TPM_DEVICE_INFO viaStarter{}, viaSystem{};
    const TBS_RESULT a = Tbsi_GetDeviceInfo(sizeof viaStarter, &viaStarter);
    wchar_t systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    HMODULE system = LoadLibraryW((std::wstring(systemPath) + L"\\tbs.dll").c_str());
    if (!system || system == local) return 4;
    using GetInfo = TBS_RESULT(WINAPI *)(UINT32, PVOID);
    const auto direct = reinterpret_cast<GetInfo>(GetProcAddress(system, "Tbsi_GetDeviceInfo"));
    if (!direct) return 5;
    const TBS_RESULT b = direct(sizeof viaSystem, &viaSystem);
    if (a != b || memcmp(&viaStarter, &viaSystem, sizeof viaStarter) != 0) {
        std::printf("tbs call differs through the starter: %08lx vs %08lx\n", static_cast<unsigned long>(a), static_cast<unsigned long>(b));
        return 6;
    }
    std::printf("tbs call through the starter: right (result %08lx)\n", static_cast<unsigned long>(a));
    const std::wstring marker = dir + L"\\runtime-attached.txt";
    for (int i = 0; i < 40 && GetFileAttributesW(marker.c_str()) == INVALID_FILE_ATTRIBUTES; i++) Sleep(50);
    return GetFileAttributesW(marker.c_str()) == INVALID_FILE_ATTRIBUTES ? 1 : 0;
}
