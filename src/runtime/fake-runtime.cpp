#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
extern "C" __declspec(dllexport) DWORD WINAPI Bo7RuntimeStart(HMODULE client,const wchar_t* folder){if(!client||!GetProcAddress(client,"XInputGetState"))return 1;std::ofstream(std::filesystem::path(folder)/L"runtime-started.txt")<<"PASS";return 0;}
