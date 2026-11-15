#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD user,DWORD flags,void* output){*static_cast<DWORD*>(output)=user+flags;return 22;}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user,void* output){*static_cast<DWORD*>(output)=user;return 11;}
extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD user,void* value){return user+*static_cast<DWORD*>(value);}
