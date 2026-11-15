#pragma once
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "minhook/include/MinHook.h"

// Independent, bounded diagnostics. All observed APIs are forwarded unchanged;
// exceptions continue to the original handlers, including after Kronos turns off.
namespace runtime_trace {
using MessageA=int(WINAPI*)(HWND,LPCSTR,LPCSTR,UINT);
using MessageW=int(WINAPI*)(HWND,LPCWSTR,LPCWSTR,UINT);
using Exit=void(WINAPI*)(UINT);
using Terminate=BOOL(WINAPI*)(HANDLE,UINT);
inline MessageA originalA=nullptr; inline MessageW originalW=nullptr;
inline Exit originalExit=nullptr; inline Terminate originalTerminate=nullptr;
inline uintptr_t engineBase=0; inline std::atomic<bool>* active=nullptr;
inline wchar_t folder[1024]{}; inline PVOID exceptionHandle=nullptr;
inline std::array<LPVOID,4> targets{};
inline std::atomic_flag memoryRecorded=ATOMIC_FLAG_INIT, exceptionRecorded=ATOMIC_FLAG_INIT,
    exitRecorded=ATOMIC_FLAG_INIT, writing=ATOMIC_FLAG_INIT;
inline std::array<char,65536> codeBytes{}; // No allocation in low-memory callbacks.

inline bool armed(){return active&&active->load(std::memory_order_acquire);}
inline void line(HANDLE file,const char* format,...) {
    char buffer[2048]{};va_list args;va_start(args,format);
    const int n=_vsnprintf_s(buffer,sizeof buffer,_TRUNCATE,format,args);va_end(args);
    const DWORD length=n<0?static_cast<DWORD>(strnlen_s(buffer,sizeof buffer)):static_cast<DWORD>(n);
    DWORD done=0;WriteFile(file,buffer,length,&done,nullptr);
}
template<class C,size_t N> inline size_t copyText(const C* source,C(&output)[N]) {
    static_assert(N>0);output[0]=0;
    if(!source)return 0;
    for(size_t i=0;i<N-1;i++){SIZE_T got=0;C c{};
        const auto address=reinterpret_cast<uintptr_t>(source);
        if(i>(UINTPTR_MAX-address)/sizeof(C)||!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address+i*sizeof(C)),&c,sizeof c,&got)||got!=sizeof c){output[i]=0;return i;}
        output[i]=c;output[i+1]=0;if(!c)return i;}
    return N-1;
}
inline bool memoryMessage(const char* text){
    if(!text)return false;char copy[1536]{};copyText(text,copy);
    for(auto& c:copy)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return strstr(copy,"memory")&&strstr(copy,"error");
}
inline void dumpFunction(HANDLE log,const char* kind,DWORD begin,DWORD end) {
    if(end<=begin||end-begin>codeBytes.size())return;
    SIZE_T got=0;const auto size=end-begin;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(engineBase+begin),codeBytes.data(),size,&got)||got!=size)return;
    wchar_t path[1200]{};wchar_t name[96]{};
    _snwprintf_s(name,_countof(name),_TRUNCATE,L"bo7-runtime-%hs-code-%x.bin",kind,begin);
    _snwprintf_s(path,_countof(path),_TRUNCATE,L"%ls\\%ls",folder,name);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;
    const bool ok=WriteFile(file,codeBytes.data(),size,&written,nullptr)&&written==size;CloseHandle(file);
    if(ok)line(log," function=0x%x..0x%x dump=bo7-runtime-%s-code-%x.bin",begin,end,kind,begin);
}
inline bool dumpData(HANDLE log,const char* kind,const char* label,uintptr_t address,size_t size){
    if(!address||!size||size>codeBytes.size()||address>UINTPTR_MAX-size)return false;
    SIZE_T got=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),codeBytes.data(),size,&got)||got!=size){
        line(log,"data %s unreadable address=0x%llx bytes=%llu\n",label,static_cast<unsigned long long>(address),static_cast<unsigned long long>(size));return false;}
    wchar_t path[1200]{};_snwprintf_s(path,_countof(path),_TRUNCATE,L"%ls\\bo7-runtime-%hs-data-%hs.bin",folder,kind,label);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;
    const bool ok=WriteFile(file,codeBytes.data(),static_cast<DWORD>(size),&written,nullptr)&&written==size;CloseHandle(file);
    if(ok)line(log,"data %s address=0x%llx bytes=%llu file=bo7-runtime-%s-data-%s.bin\n",label,static_cast<unsigned long long>(address),static_cast<unsigned long long>(size),kind,label);
    return ok;
}
inline void dumpCodeAt(HANDLE log,const char* kind,DWORD rva){
    DWORD64 image=0;auto entry=RtlLookupFunctionEntry(engineBase+rva,&image,nullptr);
    if(entry&&image==engineBase){line(log,"target exe+0x%x",rva);dumpFunction(log,kind,entry->BeginAddress,entry->EndAddress);line(log,"\n");}
    else if(!entry){MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<void*>(engineBase+rva),&region,sizeof region)&&reinterpret_cast<uintptr_t>(region.AllocationBase)==engineBase&&
           !(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))){
            line(log,"target exe+0x%x bounded-prefix-no-unwind",rva);dumpFunction(log,kind,rva,rva+256);line(log,"\n");}}
}
inline void allocatorSnapshot(HANDLE log,const char* kind,const CONTEXT& context){
    // Exact failing return established by v0.16. Never infer pool 15 from the
    // constant diagnostic suffix 15: edx selects a different allocation group.
    constexpr uintptr_t mapping=0xb4b1c88;
    if(context.Rip==engineBase+0x3a59c8d&&context.R15==engineBase&&context.R14>=mapping&&context.R14<mapping+20&&(context.R14-mapping)%4==0){
        const auto group=(context.R14-mapping)/4;int32_t mapped=-1;SIZE_T got=0;
        line(log,"allocator failed group=%llu request=%llu blockArray=0x%llx\n",group,context.Rsi,context.R12);
        dumpData(log,kind,"block-sizes",context.R12,16*8);
        dumpData(log,kind,"group-map",engineBase+mapping,5*4);
        dumpData(log,kind,"block-alignments",engineBase+0xb4a3b90,16*4);
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(engineBase+context.R14),&mapped,sizeof mapped,&got)&&got==sizeof mapped&&mapped>=0&&mapped<16){
            const auto arena=engineBase+0xe687900+static_cast<uintptr_t>(mapped)*0xacd0;
            line(log,"allocator mappedArena=%d address=0x%llx\n",mapped,static_cast<unsigned long long>(arena));
            dumpData(log,kind,"arena",arena,0xacd0);
        }
        for(const auto rva:{0x3a595e0u,0x3a5a530u,0x3a646d0u,0x3a64b10u})dumpCodeAt(log,kind,rva);
    }
    if(context.Rip==engineBase+0x3a4c86e){
        dumpData(log,kind,"load-header",context.Rbx,0x6400);
        if(context.Rbx<=UINTPTR_MAX-0x18d18)dumpData(log,kind,"load-tail",context.Rbx+0x18c00,0x118);
    }
}
inline void unwind(HANDLE log,const char* kind,CONTEXT context) {
    DWORD saved[8]{};unsigned count=0;
    for(unsigned i=0;i<24&&context.Rip&&context.Rsp;i++){
        DWORD64 image=0;auto entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
        line(log,"frame %u pc=0x%llx",i,context.Rip);
        if(image==engineBase){line(log," exe+0x%llx rbx=0x%llx rbp=0x%llx rsi=0x%llx rdi=0x%llx r12=0x%llx r13=0x%llx r14=0x%llx r15=0x%llx",
            context.Rip-image,context.Rbx,context.Rbp,context.Rsi,context.Rdi,context.R12,context.R13,context.R14,context.R15);
            bool seen=false;if(entry)for(unsigned j=0;j<count;j++)if(saved[j]==entry->BeginAddress)seen=true;
            if(entry&&!seen&&count<8){saved[count++]=entry->BeginAddress;dumpFunction(log,kind,entry->BeginAddress,entry->EndAddress);}
        }else if(image)line(log," image=0x%llx offset=0x%llx",image,context.Rip-image);
        line(log,"\n");allocatorSnapshot(log,kind,context);const auto prior=context.Rsp;
        __try {
            if(entry){void* handler=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&handler,&frame,nullptr);}
            else {SIZE_T got=0;DWORD64 next=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context.Rsp),&next,8,&got)||got!=8)break;context.Rip=next;context.Rsp+=8;}
        } __except(EXCEPTION_EXECUTE_HANDLER) {line(log,"Diagnostic unwind stopped on unreadable context.\n");break;}
        if(context.Rsp<=prior||context.Rsp-prior>0x100000)break;
    }
}
inline void capture(const char* kind,const char* message,DWORD code,const CONTEXT* supplied=nullptr,uintptr_t fault=0) {
    if(writing.test_and_set())return;
    CONTEXT context{};if(supplied)context=*supplied;else RtlCaptureContext(&context);
    wchar_t path[1200]{};_snwprintf_s(path,_countof(path),_TRUNCATE,L"%ls\\bo7-runtime-%hs-trace.log",folder,kind);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){
        line(file,"Runtime trace v0.1; kind=%s PID=%lu code=0x%lx exeBase=0x%llx fault=0x%llx\n",kind,GetCurrentProcessId(),code,static_cast<unsigned long long>(engineBase),static_cast<unsigned long long>(fault));
        if(supplied)line(file,"fault registers rax=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx rsp=0x%llx\n",context.Rax,context.Rcx,context.Rdx,context.R8,context.R9,context.Rsp);
        if(message)line(file,"message: %s\n",message);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        PROCESS_MEMORY_COUNTERS_EX process{};process.cb=sizeof process;
        if(GlobalMemoryStatusEx(&memory))line(file,"memory load=%lu%% availPhys=%llu totalPhys=%llu availPageFile=%llu totalPageFile=%llu availVirtual=%llu\n",
            memory.dwMemoryLoad,memory.ullAvailPhys,memory.ullTotalPhys,memory.ullAvailPageFile,memory.ullTotalPageFile,memory.ullAvailVirtual);
        if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof process))
            line(file,"process working=%llu peakWorking=%llu private=%llu pageFile=%llu peakPageFile=%llu\n",
                static_cast<unsigned long long>(process.WorkingSetSize),static_cast<unsigned long long>(process.PeakWorkingSetSize),
                static_cast<unsigned long long>(process.PrivateUsage),static_cast<unsigned long long>(process.PagefileUsage),static_cast<unsigned long long>(process.PeakPagefileUsage));
        unwind(file,kind,context);line(file,"Observation only; original API/exception handling continues unchanged.\n");CloseHandle(file);
    }
    writing.clear();
}
inline int WINAPI observeA(HWND window,LPCSTR text,LPCSTR title,UINT flags){
    const auto prior=GetLastError();
    if(armed()&&(memoryMessage(text)||memoryMessage(title))&&!memoryRecorded.test_and_set()){
        char copy[1536]{},caption[256]{},message[1900]{};copyText(text,copy);copyText(title,caption);
        _snprintf_s(message,sizeof message,_TRUNCATE,"title: %s\ntext: %s",caption,copy);capture("memory",message,flags);}
    SetLastError(prior);
    return originalA(window,text,title,flags);
}
inline int WINAPI observeW(HWND window,LPCWSTR text,LPCWSTR title,UINT flags){
    const auto prior=GetLastError();
    wchar_t wide[1536]{},caption[256]{};char utf8[4096]{},title8[1024]{},message[5200]{};
    if(armed()){const auto length=copyText(text,wide);WideCharToMultiByte(CP_UTF8,0,wide,static_cast<int>(length),utf8,sizeof(utf8)-1,nullptr,nullptr);
        const auto titleLength=copyText(title,caption);WideCharToMultiByte(CP_UTF8,0,caption,static_cast<int>(titleLength),title8,sizeof(title8)-1,nullptr,nullptr);
        if((memoryMessage(utf8)||memoryMessage(title8))&&!memoryRecorded.test_and_set()){
            _snprintf_s(message,sizeof message,_TRUNCATE,"title: %s\ntext: %s",title8,utf8);capture("memory",message,flags);}}
    SetLastError(prior);return originalW(window,text,title,flags);
}
inline void WINAPI observeExit(UINT code){
    const auto prior=GetLastError();
    if(armed()&&!exitRecorded.test_and_set())capture("exit","ExitProcess (may be normal or error)",code);
    SetLastError(prior);originalExit(code);
}
inline BOOL WINAPI observeTerminate(HANDLE process,UINT code){
    const auto prior=GetLastError();
    if(armed()&&GetProcessId(process)==GetCurrentProcessId()&&!exitRecorded.test_and_set())capture("exit","TerminateProcess self",code);
    SetLastError(prior);return originalTerminate(process,code);
}
inline bool isEngineInstruction(uintptr_t pc){
    // Kronos registers unwind entries for decrypted code whose AllocationBase
    // differs from the EXE base. Use its registered image identity, as unwind().
    DWORD64 image=0;return RtlLookupFunctionEntry(pc,&image,nullptr)&&image==engineBase;
}
inline LONG CALLBACK exceptionObserver(EXCEPTION_POINTERS* info){
    if(!armed()||!info||!info->ExceptionRecord||!info->ContextRecord)return EXCEPTION_CONTINUE_SEARCH;
    const auto code=info->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=0xc0000017u)return EXCEPTION_CONTINUE_SEARCH;
    const auto prior=GetLastError();
    if(code==EXCEPTION_ACCESS_VIOLATION&&!isEngineInstruction(info->ContextRecord->Rip)){SetLastError(prior);return EXCEPTION_CONTINUE_SEARCH;}
    if(!exceptionRecorded.test_and_set())capture("exception","First-chance exception; may be handled by the game",code,info->ContextRecord,
        info->ExceptionRecord->NumberParameters>1?info->ExceptionRecord->ExceptionInformation[1]:0);
    SetLastError(prior);return EXCEPTION_CONTINUE_SEARCH;
}
inline bool prepare(uintptr_t base,const std::filesystem::path& output,std::atomic<bool>* enabled){
    if(!base||!enabled||exceptionHandle)return false;
    const auto directory=output.wstring();if(directory.size()>=_countof(folder))return false;
    wcscpy_s(folder,directory.c_str());engineBase=base;active=enabled;
    auto user=GetModuleHandleW(L"user32.dll");if(!user)user=LoadLibraryExW(L"user32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto kernel=GetModuleHandleW(L"kernel32.dll");if(!user||!kernel)return false;
    targets={reinterpret_cast<LPVOID>(GetProcAddress(user,"MessageBoxA")),reinterpret_cast<LPVOID>(GetProcAddress(user,"MessageBoxW")),
        reinterpret_cast<LPVOID>(GetProcAddress(kernel,"ExitProcess")),reinterpret_cast<LPVOID>(GetProcAddress(kernel,"TerminateProcess"))};
    LPVOID originals[4]{};LPVOID callbacks[]={reinterpret_cast<LPVOID>(&observeA),reinterpret_cast<LPVOID>(&observeW),reinterpret_cast<LPVOID>(&observeExit),reinterpret_cast<LPVOID>(&observeTerminate)};
    unsigned made=0;for(;made<4;made++)if(!targets[made]||MH_CreateHook(targets[made],callbacks[made],&originals[made])!=MH_OK)break;
    if(made!=4){while(made)MH_RemoveHook(targets[--made]);return false;}
    originalA=reinterpret_cast<MessageA>(originals[0]);originalW=reinterpret_cast<MessageW>(originals[1]);
    originalExit=reinterpret_cast<Exit>(originals[2]);originalTerminate=reinterpret_cast<Terminate>(originals[3]);
    exceptionHandle=AddVectoredExceptionHandler(1,&exceptionObserver);
    if(!exceptionHandle){for(auto target:targets)MH_RemoveHook(target);return false;}
    return true;
}
}
