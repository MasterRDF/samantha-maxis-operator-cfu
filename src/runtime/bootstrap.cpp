#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <array>

static HMODULE self=nullptr;
static std::filesystem::path root(){std::array<wchar_t,32768> path{};GetModuleFileNameW(self,path.data(),static_cast<DWORD>(path.size()));return std::filesystem::path(path.data()).parent_path();}
static HMODULE original(){static std::once_flag once;static HMODULE module=nullptr;std::call_once(once,[]{module=LoadLibraryW((root()/L"bo7-runtime-original.dll").c_str());});return module;}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD user,DWORD flags,void* value){using Function=DWORD(WINAPI*)(DWORD,DWORD,void*);const auto module=original();auto fn=module?reinterpret_cast<Function>(GetProcAddress(module,"XInputGetCapabilities")):nullptr;return fn?fn(user,flags,value):ERROR_DEVICE_NOT_CONNECTED;}
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user,void* value){using Function=DWORD(WINAPI*)(DWORD,void*);const auto module=original();auto fn=module?reinterpret_cast<Function>(GetProcAddress(module,"XInputGetState")):nullptr;return fn?fn(user,value):ERROR_DEVICE_NOT_CONNECTED;}
extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD user,void* value){using Function=DWORD(WINAPI*)(DWORD,void*);const auto module=original();auto fn=module?reinterpret_cast<Function>(GetProcAddress(module,"XInputSetState")):nullptr;return fn?fn(user,value):ERROR_DEVICE_NOT_CONNECTED;}
static DWORD WINAPI start(void*){const auto client=original();if(!client)return 1;const auto directory=root();const auto module=LoadLibraryExW((directory/L"bo7-runtime.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module)return 2;using Start=DWORD(WINAPI*)(HMODULE,const wchar_t*);const auto fn=reinterpret_cast<Start>(GetProcAddress(module,"Bo7RuntimeStart"));return fn?fn(client,directory.c_str()):3;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);if(const auto thread=CreateThread(nullptr,0,start,nullptr,0,nullptr))CloseHandle(thread);}return TRUE;}
