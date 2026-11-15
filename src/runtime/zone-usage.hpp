#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <cstring>
#include "minhook/include/MinHook.h"

// Call sites in the actual captured 3ad41b0 and 382c770 routines establish
// 3a50310(ctx,dst,n). 3ad41b0 takes six pointer/integer arguments and returns
// its status in eax. No profile-only function address is used here.
namespace zone_usage {
struct Block {uintptr_t data;uint64_t size;};
inline uint64_t ExpectedBlock10Capacity=65536;
using Read=uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uint64_t);
using Assets=uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
inline Read originalRead=nullptr;inline Assets originalAssets=nullptr;
inline const char* setupStage="not-started";inline MH_STATUS setupStatus=MH_OK;
inline const char* entryReason="not-checked";
inline uintptr_t entryAllocationBase=0;inline DWORD entryProtection=0;
inline std::atomic<uintptr_t> owner{0},start{0};
inline std::atomic<uint64_t> capacity{0},highWater{0},cursorPeak{0},finalCursor{0},reads{0};
inline std::atomic<uint64_t> readCalls{0},assetCalls{0};
inline std::atomic<bool> overflow{false},finalCaptured{false};
inline std::atomic<bool> candidateCaptured{false};
inline std::array<unsigned char,0xc40> finalContext{};
inline std::array<unsigned char,0xc40> candidateContext{};
inline bool modBlocks(const std::array<Block,16>& b){
    // Native v0.13 context shows that block 2 has already been discarded.
    return b[1].size==57246134&&(b[2].size==0||b[2].size==96796566)&&b[3].size==54963954&&
        b[10].size==ExpectedBlock10Capacity&&b[11].size==54469094&&b[15].size==125663043;
}
inline bool readBlocks(uintptr_t context,std::array<Block,16>& blocks){
    SIZE_T got=0;return context<=UINTPTR_MAX-8-sizeof blocks&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context+8),blocks.data(),sizeof blocks,&got)&&got==sizeof blocks;
}
inline void bindContext(uintptr_t context){
    if(owner.load(std::memory_order_acquire))return;std::array<Block,16> blocks{};
    if(readBlocks(context,blocks)&&modBlocks(blocks)&&blocks[10].data&&blocks[10].data<=UINTPTR_MAX-blocks[10].size){
        capacity.store(blocks[10].size,std::memory_order_relaxed);start.store(blocks[10].data,std::memory_order_relaxed);
        owner.store(context,std::memory_order_release);
    }
}
inline void maximum(std::atomic<uint64_t>& target,uint64_t value){auto previous=target.load(std::memory_order_relaxed);while(previous<value&&!target.compare_exchange_weak(previous,value,std::memory_order_relaxed)){};}
inline bool cursorValue(uintptr_t context,uint64_t& value){
    // Confirmed by a native context capture: Block[16] +8, current pos +108,
    // saved pos[16] +110, current block index +190.
    std::array<unsigned char,0x90> state{};SIZE_T got=0;
    if(context>UINTPTR_MAX-0x198||!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context+0x108),state.data(),state.size(),&got)||got!=state.size())return false;
    uint32_t block=0;uintptr_t position=0;std::memcpy(&block,state.data()+0x88,4);
    if(block>=16)return false;std::memcpy(&position,state.data()+(block==10?0:0x58),8);
    const auto base=start.load(),limit=capacity.load();if(position<base||position-base>limit)return false;
    value=position-base;return true;
}
inline void recordRead(uintptr_t context,uintptr_t destination,uint64_t bytes){
    bindContext(context);if(owner.load(std::memory_order_acquire)!=context)return;
    const auto base=start.load(),limit=capacity.load();
    if(base&&destination>=base&&destination-base<limit){
        const auto offset=destination-base;if(bytes>limit-offset)overflow.store(true);
        else {maximum(highWater,offset+bytes);reads.fetch_add(1);}
    }
    uint64_t cursor=0;if(cursorValue(context,cursor))maximum(cursorPeak,cursor);
}
inline uintptr_t __fastcall observeRead(uintptr_t context,uintptr_t destination,uint64_t bytes){
    readCalls.fetch_add(1,std::memory_order_relaxed);
    const auto result=originalRead(context,destination,bytes);const auto error=GetLastError();
    recordRead(context,destination,bytes);SetLastError(error);return result;
}
inline uintptr_t __fastcall observeAssets(uintptr_t context,uintptr_t record,uintptr_t list,uintptr_t output,uintptr_t fifth,uintptr_t sixth){
    assetCalls.fetch_add(1,std::memory_order_relaxed);
    const auto result=originalAssets(context,record,list,output,fifth,sixth);const auto error=GetLastError();bindContext(context);
    if(result==0&&!candidateCaptured.load(std::memory_order_acquire)&&list<=UINTPTR_MAX-0x18){
        std::array<uint32_t,2> counts{};SIZE_T got=0;
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(list+0x10),counts.data(),sizeof counts,&got)&&got==sizeof counts&&counts[0]==834&&counts[1]==834&&
           ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context),candidateContext.data(),candidateContext.size(),&got)&&got==candidateContext.size())candidateCaptured.store(true,std::memory_order_release);
    }
    if(result==0&&owner.load()==context&&!finalCaptured.load(std::memory_order_acquire)){
        std::array<uint32_t,2> counts{};std::array<Block,16> blocks{};SIZE_T got=0;
        if(list<=UINTPTR_MAX-0x18&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(list+0x10),counts.data(),sizeof counts,&got)&&got==sizeof counts&&
           counts[0]==834&&counts[1]==834&&readBlocks(context,blocks)&&modBlocks(blocks)){
            uint64_t cursor=0;if(cursorValue(context,cursor)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(context),finalContext.data(),finalContext.size(),&got)&&got==finalContext.size()){
                finalCursor.store(cursor);maximum(cursorPeak,cursor);finalCaptured.store(true,std::memory_order_release);
            }
        }
    }
    SetLastError(error);return result;
}
inline void writeReport(const std::filesystem::path& folder){
    const bool complete=finalCaptured.load(std::memory_order_acquire);std::ofstream out(folder/L"bo7-runtime-block10-usage.log",std::ios::trunc);
    out<<"Block 10 observation v0.1 PID="<<GetCurrentProcessId()<<"; native state 2 reached\n"
       <<"rangeStart=0x"<<std::hex<<start.load()<<std::dec<<" capacity="<<capacity.load()
       <<" fileReadHighWater="<<highWater.load()<<" nativeCursorPeak="<<cursorPeak.load()<<" finalCursor="<<finalCursor.load()
       <<" completedAssets="<<complete<<" observedReads="<<reads.load()<<" overflow="<<overflow.load()<<"\n"
       <<"readCallbacks="<<readCalls.load()<<" assetCallbacks="<<assetCalls.load()<<" candidateCaptured="<<candidateCaptured.load(std::memory_order_acquire)<<"\n"
       <<"Current-build read/asset-list routines observed; six arguments and native result preserved; no allocation size changed.\n";
    if(complete){std::ofstream data(folder/L"bo7-runtime-final-load-context.bin",std::ios::binary|std::ios::trunc);data.write(reinterpret_cast<const char*>(finalContext.data()),finalContext.size());}
    if(candidateCaptured.load(std::memory_order_acquire)){std::ofstream data(folder/L"bo7-runtime-candidate-load-context.bin",std::ios::binary|std::ios::trunc);data.write(reinterpret_cast<const char*>(candidateContext.data()),candidateContext.size());}
}
inline bool validReadEntry(uintptr_t base,uintptr_t address){
    MEMORY_BASIC_INFORMATION region{};
    entryReason="address";if(!base||address<base||address-base>UINT32_MAX)return false;
    entryReason="query";if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof region))return false;
    entryAllocationBase=reinterpret_cast<uintptr_t>(region.AllocationBase);entryProtection=region.Protect;
    entryReason="page-protection";if(region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
       !(region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    unsigned char prefix[32]{};SIZE_T got=0;
    entryReason="read-prefix";if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),prefix,sizeof prefix,&got)||got!=sizeof prefix||prefix[0]==0||prefix[0]==0xcc)return false;
    DWORD64 image=0;const auto entry=RtlLookupFunctionEntry(address,&image,nullptr);
    if(!entry){const bool leaf=entryAllocationBase==base;entryReason=leaf?"leaf-ok":"leaf-allocation-base";return leaf;}
    // A registered image/function can span separate Windows allocations.
    // Establish ownership using its unwind image, not AllocationBase equality.
    const bool ok=image==base&&entry->EndAddress-entry->BeginAddress<=65536&&
        (entry->BeginAddress==address-base||prefix[0]==0xe9||prefix[0]==0xeb||(prefix[0]==0xff&&prefix[1]==0x25));
    entryReason=ok?"unwind-ok":"unwind-owner-or-entry";return ok;
}
inline bool prepare(uintptr_t base){
    setupStage="arguments";if(!base||originalRead||originalAssets)return false;
    const auto read=base+0x3a50310,assets=base+0x3ad41b0;
    setupStage="asset-list-unwind";DWORD64 image=0;const auto entry=RtlLookupFunctionEntry(base+0x3ad43d1,&image,nullptr);
    if(!entry||image!=base||entry->BeginAddress!=0x3ad41b0||entry->EndAddress!=0x3ad442c)return false;
    // Collect page diagnostics, then use the captured entry/bytes below as the
    // authoritative target guard. MinHook separately checks executability.
    (void)validReadEntry(base,read);
    setupStage="file-read-exact-code";const std::array<unsigned char,15> expected{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    std::array<unsigned char,15> actual{};SIZE_T got=0;DWORD64 readImage=0;
    const auto readEntry=RtlLookupFunctionEntry(read+0x15,&readImage,nullptr);
    if(!readEntry||readImage!=base||readEntry->BeginAddress!=0x3a50310||readEntry->EndAddress!=0x3a50370||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(read),actual.data(),actual.size(),&got)||got!=actual.size()||actual!=expected)return false;
    LPVOID original=nullptr;setupStage="file-read-hook";
    setupStatus=MH_CreateHook(reinterpret_cast<LPVOID>(read),reinterpret_cast<LPVOID>(&observeRead),&original);if(setupStatus!=MH_OK)return false;
    originalRead=reinterpret_cast<Read>(original);setupStage="asset-list-hook";
    setupStatus=MH_CreateHook(reinterpret_cast<LPVOID>(assets),reinterpret_cast<LPVOID>(&observeAssets),&original);
    if(setupStatus!=MH_OK){MH_RemoveHook(reinterpret_cast<LPVOID>(read));originalRead=nullptr;return false;}
    originalAssets=reinterpret_cast<Assets>(original);setupStage="ready";return true;
}
}
