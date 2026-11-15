#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include "dropin-config.hpp"
#include "dropin-runtime-pin.hpp"
#include "tbs_exports.h"

namespace {
namespace fs=std::filesystem;
HMODULE self=nullptr;
void log(const fs::path& game,const std::string& message){
    std::error_code error;fs::create_directories(game/L"kronos"/L"Logs",error);
    std::ofstream file(game/L"kronos"/L"Logs"/L"maxis-loader.log",std::ios::app);
    file<<"PID "<<GetCurrentProcessId()<<": "<<message<<'\n';
}
std::string hash(const fs::path& path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Maxis runtime DLL is missing.");
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE handle=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable.");
    const auto cleanup=[&](){if(handle)BCryptDestroyHash(handle);BCryptCloseAlgorithmProvider(algorithm,0);};
    if(BCryptCreateHash(algorithm,&handle,nullptr,0,nullptr,0,0)<0){cleanup();throw std::runtime_error("SHA256 initialization failed.");}
    std::array<unsigned char,65536> buffer{};
    while(file){file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());const auto bytes=file.gcount();if(bytes&&BCryptHashData(handle,buffer.data(),static_cast<ULONG>(bytes),0)<0){cleanup();throw std::runtime_error("SHA256 read failed.");}}
    std::array<unsigned char,32> digest{};
    if(!file.eof()||BCryptFinishHash(handle,digest.data(),static_cast<ULONG>(digest.size()),0)<0){cleanup();throw std::runtime_error("SHA256 failed.");}
    cleanup();std::ostringstream text;for(const auto byte:digest)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);return text.str();
}
DWORD WINAPI start(void*){
    std::array<wchar_t,32768> own{},exe{};
    if(!GetModuleFileNameW(self,own.data(),static_cast<DWORD>(own.size()))||!GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size())))return 1;
    const auto game=fs::path(own.data()).parent_path();
    if(_wcsicmp(fs::path(exe.data()).filename().c_str(),L"cod25-cod.exe")!=0)return 0;
    try{
        log(game,"Maxis drag-and-drop loader v0.1 starting.");
        if(!maxis_dropin::enabled(game)){log(game,"Library mod is disabled or absent; nothing loaded.");return 0;}
        const auto folder=maxis_dropin::folder(game)/L"native";
        const auto runtime=folder/L"maxis-runtime.dll";
        // Refuse reparse paths so the fixed mod namespace stays inside this game.
        for(auto path=runtime;path!=game&&path.has_parent_path();path=path.parent_path()){
            const auto attributes=GetFileAttributesW(path.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse mod paths are unsupported.");
        }
        if(hash(runtime)!=MaxisRuntimeSha256)throw std::runtime_error("Maxis runtime DLL checksum differs from this loader.");
        HMODULE kronos=nullptr;
        for(unsigned attempt=0;attempt<600&&!kronos;++attempt){kronos=GetModuleHandleW(L"XINPUT9_1_0.dll");if(!kronos)Sleep(100);}
        if(!kronos)throw std::runtime_error("Kronos did not load within 60 seconds.");
        std::array<wchar_t,32768> loaded{};
        if(!GetModuleFileNameW(kronos,loaded.data(),static_cast<DWORD>(loaded.size()))||_wcsicmp(loaded.data(),(game/L"XINPUT9_1_0.dll").c_str()))throw std::runtime_error("Kronos is not loaded from this game folder.");
        const auto module=LoadLibraryExW(runtime.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)throw std::runtime_error("Maxis runtime DLL could not be loaded.");
        using Attach=DWORD(WINAPI*)(void*);
        const auto attach=reinterpret_cast<Attach>(GetProcAddress(module,"Bo7RuntimeAttach"));
        if(!attach)throw std::runtime_error("Maxis runtime attach export is missing.");
        const auto result=attach(nullptr);
        if(result!=ERROR_SUCCESS&&result!=ERROR_ALREADY_INITIALIZED)throw std::runtime_error("Maxis runtime initialization returned "+std::to_string(result)+".");
        log(game,"Started dedicated Maxis runtime automatically; shared active-mod/installed JSON was not used.");
        return 0;
    }catch(const std::exception& error){log(game,std::string("Refused: ")+error.what());return 1;}
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){
    if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);if(const auto thread=CreateThread(nullptr,0,start,nullptr,0,nullptr))CloseHandle(thread);}
    return TRUE;
}
