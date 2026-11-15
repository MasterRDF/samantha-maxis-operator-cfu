#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "cfu-error-profile.hpp"
#include "fatal-trace.hpp"
#include "ops_patch.h"
#include "native_template.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <vector>

static unsigned checks=0;
static void check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;}
static std::vector<uint8_t> readFile(const wchar_t* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static unsigned forwarded=0;
static uintptr_t __fastcall originalError(int code,int level,const char* message,void* extra){
    check(code==123&&level==7&&message==nullptr&&extra==reinterpret_cast<void*>(0x1234),"all callback arguments preserved");
    check(GetLastError()==987,"incoming LastError preserved");++forwarded;SetLastError(654);return 0xfeedbeef;
}
int wmain(int argc,wchar_t** argv){
 try{
    check(argc==3,"fixture input paths");const auto file=readFile(argv[1]),manifest=readFile(argv[2]);
    check(file.size()>4096&&manifest.size()==16+789*8,"fixture inputs");
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    const auto bytes=nt->OptionalHeader.SizeOfImage;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(image!=nullptr,"fixture image allocation");memcpy(image,file.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    const auto base=reinterpret_cast<uintptr_t>(image);
    const auto reader=[](uintptr_t address,void* target,size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),target,size,&got)&&got==size;};
    check(client_error_profile::matches(reader,base),"stock callback accepted");
    check(!cfu_error_profile::matches(reader,base),"stock callback not misclassified as CFU");
    mwcfops::Input input;input.operators.resize(128);input.skins.resize(661);
    memcpy(input.operators.data(),manifest.data()+16,128*8);memcpy(input.skins.data(),manifest.data()+16+128*8,661*8);
    mwcfops::Result patch;std::string error;
    check(mwcfops::Install(image,bytes,input,patch,error),"actual pinned CFU patch installed in offline image");
    check(!client_error_profile::matches(reader,base),"original conflict reproduced");
    check(cfu_error_profile::matches(reader,base),"actual CFU patch accepted");
    check(sizeof(kGuardTemplate)==cfu_error_profile::Guard.size()&&!memcmp(kGuardTemplate,cfu_error_profile::Guard.data(),sizeof(kGuardTemplate)),"profile template matches original CFU implementation");
    for(const auto rva:{client_error_profile::HookRva,client_error_profile::InitializerRva,client_error_profile::TailRva,client_error_profile::TailRva+5,client_error_profile::TailRva+12}){
        image[rva]^=1;check(!cfu_error_profile::matches(reader,base),"changed callback refused");image[rva]^=1;
    }
    DWORD old=0;check(VirtualProtect(patch.code,4096,PAGE_READWRITE,&old)!=FALSE,"fixture guard writable");
    for(size_t i=0;i<cfu_error_profile::Guard.size();++i){patch.code[i]^=1;check(!cfu_error_profile::matches(reader,base),"changed guard or relocation refused");patch.code[i]^=1;}
    DWORD ignored=0;VirtualProtect(patch.code,4096,old,&ignored);
    InterlockedIncrement(patch.counter);check(cfu_error_profile::matches(reader,base),"live CFU counter allowed");
    const auto unreadable=[&](uintptr_t a,void* b,size_t c){return a!=reinterpret_cast<uintptr_t>(patch.code)&&reader(a,b,c);};
    check(!cfu_error_profile::matches(unreadable,base),"unreadable guard refused");
    check(!cfu_error_profile::relative(1,-2),"negative relative overflow refused");
    check(!cfu_error_profile::relative(UINTPTR_MAX,1),"positive relative overflow refused");
    std::vector<uint8_t> imageBefore(image,image+bytes),guardBefore(patch.code,patch.code+cfu_error_profile::Guard.size());
    auto* cell=reinterpret_cast<void* volatile*>(image+client_error_profile::CellRva);*cell=reinterpret_cast<void*>(&originalError);
    memcpy(imageBefore.data()+client_error_profile::CellRva,const_cast<void**>(cell),sizeof(void*));
    check(fatal_trace::install(cell,&originalError,base,L"unused-fixture-log"),"fatal observer chains existing callback cell");
    SetLastError(987);const auto result=reinterpret_cast<fatal_trace::Function>(*cell)(123,7,nullptr,reinterpret_cast<void*>(0x1234));
    check(result==0xfeedbeef&&forwarded==1&&GetLastError()==654,"return value and outgoing LastError forwarded");
    memcpy(imageBefore.data()+client_error_profile::CellRva,const_cast<void**>(cell),sizeof(void*));
    check(!memcmp(imageBefore.data(),image,bytes)&&!memcmp(guardBefore.data(),patch.code,guardBefore.size()),"only callback cell changed; CFU guard and tables preserved");
    check(cfu_error_profile::matches(reader,base),"profile remains valid after observer installation");
    VirtualFree(patch.allocation,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"PASS: "<<checks<<" CFU callback integration checks. No game process launched or attached.\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
