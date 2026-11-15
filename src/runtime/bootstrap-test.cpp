#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <cstdio>
int wmain(int argc,wchar_t** argv){if(argc!=2)return 1;const auto root=std::filesystem::absolute(argv[1]);const auto module=LoadLibraryW((root/L"XINPUT9_1_0.dll").c_str());if(!module)return 2;
    using Get=DWORD(WINAPI*)(DWORD,void*);using Caps=DWORD(WINAPI*)(DWORD,DWORD,void*);
    const auto caps=reinterpret_cast<Caps>(GetProcAddress(module,"XInputGetCapabilities"));const auto get=reinterpret_cast<Get>(GetProcAddress(module,"XInputGetState"));const auto set=reinterpret_cast<Get>(GetProcAddress(module,"XInputSetState"));
    DWORD value=0;if(!caps||!get||!set||caps(3,4,&value)!=22||value!=7)return 3;if(get(8,&value)!=11||value!=8||set(9,&value)!=17)return 4;
    for(unsigned i=0;i<60&&!std::filesystem::exists(root/L"runtime-started.txt");++i)Sleep(50);
    if(!std::filesystem::exists(root/L"runtime-started.txt"))return 5;
    std::puts("PASS: thin bootstrap forwards all three exports and starts a separate runtime with the original client handle; private stub DLLs only.");return 0;}
