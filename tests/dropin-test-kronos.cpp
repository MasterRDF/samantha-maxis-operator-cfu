// Stand-in for Kronos's XINPUT9_1_0.dll in the starter's offline test.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user, void *output) { *static_cast<DWORD *>(output) = user; return 11; }
BOOL WINAPI DllMain(HINSTANCE, DWORD, void *) { return TRUE; }
