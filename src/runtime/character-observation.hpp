#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include "third-party/json.hpp"
#include "minhook/include/MinHook.h"

namespace character_observation {
using Json=nlohmann::json;
using Load=int(__fastcall*)(const char*,const void*,uint64_t,uint64_t,unsigned char,unsigned char,uint32_t);
using List=int(__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,unsigned char);
using Read=uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uint64_t);
using HashFile=std::string(*)(const std::filesystem::path&);
struct Block {uintptr_t address=0;uint64_t bytes=0;};
struct Frame {std::array<Block,16> blocks{};uintptr_t current=0;std::array<uintptr_t,16> saved{};uint32_t activeBlock=0;uint16_t ownerZone=0;};
struct Model {uint64_t hash=0;uint32_t ordinal=0;uint16_t surfaces=0,clients=0;uint8_t normal=0,lods=0;std::array<uint64_t,8> lodHashes{};};
struct Profile {std::string name,manifestSha;uint32_t assets=0;std::array<uint64_t,16> capacities{};std::array<Model,3> models{};size_t modelCount=0;};
struct Usage {uint64_t readEnd=0,cursorPeak=0,reads=0;bool overflow=false,invalidCursor=false;};
struct ModelResult {bool readable=false,matches=false;const char* reason="not-observed";uint64_t identity=0;uint16_t surfaces=0,clients=0;uint8_t normal=0,lods=0;};
struct ReadEvidence {uint64_t call=0,bytes=0;uintptr_t destination=0;bool beforeValid=false;Frame before{},after{};};
struct ReadRecord {uint64_t call=0,bytes=0;uintptr_t destination=0,callerRva=0,beforeCursor=0,afterCursor=0,regionBase=0;uint64_t regionBytes=0;uint32_t activeBlock=0,regionState=0,regionProtect=0;bool beforeValid=false;};
struct CodeRegion {uintptr_t seedRva=0,allocationBase=0,address=0;uint64_t bytes=0;uint32_t state=0,protect=0;};
struct Session {
    unsigned ticket=0;uintptr_t context=0;Frame first{},last{};std::array<Usage,16> usage{};
    uint64_t readCalls=0,unmappedReads=0;bool contextConflict=false,capacityMismatch=false,rangeInvalid=false,ambiguousRead=false,truncated=false;
    bool listSeen=false,listComplete=false;uint32_t declared=0,consumed=0;int listResult=-1;
    std::array<ModelResult,3> models{};
    // Four bounded examples per category; preserve the original conservative flags.
    std::array<std::array<ReadEvidence,4>,4> evidence{};
    std::array<unsigned,4> evidenceCounts{};
    // Complete for small selected controls; explicitly bounded for larger packages.
    std::array<ReadRecord,512> readLedger{};unsigned ledgerCount=0;bool ledgerTruncated=false;
    std::array<uint32_t,24> codeFunctions{};unsigned codeFunctionCount=0;
    std::array<uint64_t,7> codeRejects{};std::array<CodeRegion,24> codeRegions{};unsigned codeRegionCount=0;
};
inline Profile profile{};
inline uintptr_t imageBase=0;
inline std::filesystem::path reportFolder;
inline Load originalLoad=nullptr;
inline List originalList=nullptr;
inline Read originalRead=nullptr;
inline std::atomic<bool> enabled{false};
inline std::atomic<unsigned> tickets{0};
inline thread_local Session* currentSession=nullptr;
inline HANDLE mainLease=INVALID_HANDLE_VALUE;
inline constexpr uint64_t MaxReads=2000000;
inline bool copy(uintptr_t address,void* target,size_t bytes){
    SIZE_T copied=0;return address&&bytes<=UINTPTR_MAX-address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),target,bytes,&copied)&&copied==bytes;
}
template<class Value> inline Value scalar(const unsigned char* bytes,size_t offset){Value result{};std::memcpy(&result,bytes+offset,sizeof result);return result;}
inline uint64_t integer(const Json& value){
    if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<int64_t>()<0))throw std::runtime_error("Diagnostic integer must be nonnegative.");
    return value.get<uint64_t>();
}
inline uint64_t identity(const Json& value){
    const auto text=value.get<std::string>();
    if(!std::regex_match(text,std::regex("[0-7][0-9a-f]{15}")))throw std::runtime_error("Diagnostic hash must be a 63-bit hexadecimal identity.");
    const auto result=std::stoull(text,nullptr,16);if(!result)throw std::runtime_error("Empty diagnostic identity.");return result;
}
inline Profile parseProfile(const Json& manifest,const std::string& manifestSha){
    const auto& diagnostic=manifest.at("diagnosticCharacterObservation");
    if(diagnostic.at("schemaVersion")!=1||diagnostic.at("enabled")!=true)throw std::runtime_error("Character observation requires explicit opt-in.");
    Profile result;result.name=manifest.at("name").get<std::string>();result.manifestSha=manifestSha;
    if(!std::regex_match(result.name,std::regex("[a-z][a-z0-9_]{0,47}"))||!std::regex_match(manifestSha,std::regex("[a-f0-9]{64}")))throw std::runtime_error("Unbound diagnostic package.");
    const auto assets=integer(manifest.at("assetCount"));if(!assets||assets>1000000)throw std::runtime_error("Invalid diagnostic asset count.");result.assets=static_cast<uint32_t>(assets);
    const auto& capacities=manifest.at("memoryCapacities");if(!capacities.is_array()||capacities.size()!=16)throw std::runtime_error("Expected sixteen capacity entries.");
    for(size_t index=0;index<16;++index)result.capacities[index]=integer(capacities[index]);
    const auto& models=diagnostic.at("models");if(!models.is_array()||models.empty()||models.size()>3)throw std::runtime_error("Expected one to three model contracts.");
    for(const auto& entry:models){
        Model model;model.hash=identity(entry.at("hash"));const auto ordinal=integer(entry.at("ordinal"));
        const auto surfaces=integer(entry.at("surfaceCount")),normal=integer(entry.at("normalBones")),clients=integer(entry.at("clientBones"));
        const auto& lods=entry.at("lodHashes");
        if(ordinal>=result.assets||!surfaces||surfaces>65535||!normal||normal>254||clients>32767-normal||!lods.is_array()||lods.empty()||lods.size()>8)throw std::runtime_error("Invalid native model contract.");
        model.ordinal=static_cast<uint32_t>(ordinal);model.surfaces=static_cast<uint16_t>(surfaces);model.normal=static_cast<uint8_t>(normal);model.clients=static_cast<uint16_t>(clients);model.lods=static_cast<uint8_t>(lods.size());
        for(size_t index=0;index<lods.size();++index)model.lodHashes[index]=identity(lods[index]);
        for(size_t index=0;index<result.modelCount;++index)if(result.models[index].hash==model.hash||result.models[index].ordinal==model.ordinal)throw std::runtime_error("Colliding model contracts.");
        result.models[result.modelCount++]=model;
    }
    return result;
}
inline bool readFrame(uintptr_t context,Frame& frame){
    if(context>UINTPTR_MAX-0xc3a)return false;
    uintptr_t vtable=0;
    return copy(context,&vtable,8)&&vtable==imageBase+0xb4b27a8&&copy(context+8,frame.blocks.data(),sizeof frame.blocks)&&
        copy(context+0x108,&frame.current,8)&&copy(context+0x110,frame.saved.data(),sizeof frame.saved)&&
        copy(context+0x190,&frame.activeBlock,4)&&frame.activeBlock<16&&copy(context+0xc38,&frame.ownerZone,2)&&frame.ownerZone>0&&frame.ownerZone<4096;
}
inline bool sample(Session& session,uintptr_t context){
    Frame frame;if(!readFrame(context,frame))return false;
    if(session.context&&session.context!=context){session.contextConflict=true;return false;}
    if(!session.context){session.context=context;session.first=frame;}
    session.last=frame;
    for(size_t index=0;index<16;++index){
        const auto block=frame.blocks[index];auto& usage=session.usage[index];
        if(block.bytes!=profile.capacities[index]&&!(index==2&&block.bytes==0))session.capacityMismatch=true;
        if(block.bytes&&(!block.address||block.bytes>UINTPTR_MAX-block.address)){session.rangeInvalid=true;continue;}
        const auto cursor=frame.activeBlock==index?frame.current:frame.saved[index];
        if(!cursor||(index==2&&!block.bytes))continue;
        if(!block.bytes||cursor<block.address||cursor-block.address>block.bytes){usage.invalidCursor=true;continue;}
        usage.cursorPeak=std::max(usage.cursorPeak,static_cast<uint64_t>(cursor-block.address));
    }
    return true;
}
inline void recordRead(Session& session,uintptr_t context,uintptr_t destination,uint64_t bytes,const Frame* before=nullptr,uintptr_t callerRva=0){
    if(++session.readCalls>MaxReads){session.truncated=true;return;}
    if(!sample(session,context))return;
    size_t matched=0;bool crossing=false,invalidCursor=false,capacityMismatch=false;
    for(size_t index=0;index<16;++index){
        const auto block=session.last.blocks[index];
        if(block.bytes!=profile.capacities[index]&&!(index==2&&block.bytes==0))capacityMismatch=true;
        const auto cursor=session.last.activeBlock==index?session.last.current:session.last.saved[index];
        if(cursor&&!(index==2&&!block.bytes)&&(!block.bytes||cursor<block.address||cursor-block.address>block.bytes))invalidCursor=true;
    }
    for(size_t index=0;index<16;++index){
        const auto block=session.last.blocks[index];if(!block.address||!block.bytes||block.bytes>UINTPTR_MAX-block.address)continue;
        if(destination<block.address||destination-block.address>=block.bytes)continue;
        ++matched;auto& usage=session.usage[index];const auto offset=destination-block.address;
        if(bytes>block.bytes-offset){usage.overflow=true;crossing=true;}
        else {usage.readEnd=std::max(usage.readEnd,offset+bytes);++usage.reads;}
    }
    if(!matched){++session.unmappedReads;const auto active=session.last.blocks[session.last.activeBlock];
        if(bytes&&active.address&&active.bytes<=UINTPTR_MAX-active.address&&destination==active.address+active.bytes){session.usage[session.last.activeBlock].overflow=true;crossing=true;}}
    if(matched>1)session.ambiguousRead=true;
    if(session.ledgerCount<session.readLedger.size()){
        auto& item=session.readLedger[session.ledgerCount++];item.call=session.readCalls;item.bytes=bytes;item.destination=destination;item.callerRva=callerRva;
        item.beforeValid=before!=nullptr;item.beforeCursor=before?before->current:0;item.afterCursor=session.last.current;item.activeBlock=session.last.activeBlock;
        if(!matched){MEMORY_BASIC_INFORMATION region{};if(VirtualQuery(reinterpret_cast<const void*>(destination),&region,sizeof region)){
            item.regionBase=reinterpret_cast<uintptr_t>(region.BaseAddress);item.regionBytes=region.RegionSize;item.regionState=region.State;item.regionProtect=region.Protect;}}
    }else session.ledgerTruncated=true;
    const std::array<bool,4> reasons{capacityMismatch,matched==0,crossing,invalidCursor};
    for(size_t kind=0;kind<reasons.size();++kind){
        if(!reasons[kind]||session.evidenceCounts[kind]>=4)continue;
        auto& event=session.evidence[kind][session.evidenceCounts[kind]++];
        event.call=session.readCalls;event.bytes=bytes;event.destination=destination;
        event.beforeValid=before!=nullptr;if(before)event.before=*before;event.after=session.last;
    }
}
inline Json frameEvidence(const Frame& frame){
    Json blocks=Json::array();
    for(size_t i=0;i<16;++i)blocks.push_back({{"index",i},{"address",frame.blocks[i].address},{"bytes",frame.blocks[i].bytes},{"savedCursor",frame.saved[i]}});
    return {{"activeBlock",frame.activeBlock},{"currentCursor",frame.current},{"ownerZone",frame.ownerZone},{"blocks",blocks}};
}
inline ModelResult inspectModel(uintptr_t pointer,const Model& expected){
    ModelResult result;std::array<unsigned char,216> root{};result.reason="root-unreadable";
    if(!copy(pointer,root.data(),root.size()))return result;
    result.readable=true;result.identity=scalar<uint64_t>(root.data(),0)&0x7fffffffffffffff;
    result.surfaces=scalar<uint16_t>(root.data(),0x10);result.lods=root[0x12];
    result.reason="identity";if(result.identity!=expected.hash)return result;
    result.reason="surface-count";if(result.surfaces!=expected.surfaces)return result;
    result.reason="lod-count";if(result.lods!=expected.lods)return result;
    std::array<unsigned char,8> skeleton{};result.reason="skeleton-unreadable";
    if(!copy(scalar<uintptr_t>(root.data(),0x60),skeleton.data(),skeleton.size()))return result;
    result.normal=skeleton[0];result.clients=scalar<uint16_t>(skeleton.data(),6);result.reason="bone-partition";
    if(result.normal!=expected.normal||result.clients!=expected.clients)return result;
    std::array<unsigned char,8*72> lods{};result.reason="lod-array-unreadable";
    if(!copy(scalar<uintptr_t>(root.data(),0x90),lods.data(),expected.lods*72))return result;
    for(size_t index=0;index<expected.lods;++index){uint64_t hash=0;result.reason="surface-identity";
        if(!copy(scalar<uintptr_t>(lods.data(),index*72),&hash,8)||(hash&0x7fffffffffffffff)!=expected.lodHashes[index])return result;}
    result.matches=true;result.reason="ok";return result;
}
inline void recordList(Session& session,uintptr_t context,uintptr_t list,int result){
    if(!sample(session,context))return;
    std::array<unsigned char,48> header{};if(!copy(list,header.data(),header.size()))return;
    session.listSeen=true;session.listResult=result;session.declared=scalar<uint32_t>(header.data(),0x10);session.consumed=scalar<uint32_t>(header.data(),0x14);
    session.listComplete=result==0&&session.declared==profile.assets&&session.consumed==profile.assets;
    if(!session.listComplete)return;
    const auto entries=scalar<uintptr_t>(header.data(),0x28);
    for(size_t index=0;index<profile.modelCount;++index){const auto& model=profile.models[index];std::array<uint64_t,2> entry{};
        if(!entries||entries>UINTPTR_MAX-static_cast<uint64_t>(model.ordinal)*16||!copy(entries+static_cast<uint64_t>(model.ordinal)*16,entry.data(),sizeof entry)){session.models[index].reason="entry-unreadable";continue;}
        if(entry[0]!=8){session.models[index].reason="entry-type";continue;}
        session.models[index]=inspectModel(entry[1],model);
    }
}
inline Json report(const Session& session,int loadResult){
    Json blocks=Json::array(),models=Json::array();bool ranges=true,modelsMatch=session.listComplete;
    for(size_t index=0;index<16;++index){const auto& usage=session.usage[index];ranges=ranges&&!usage.overflow&&!usage.invalidCursor;
        blocks.push_back({{"index",index},{"expectedBytes",profile.capacities[index]},{"firstBytes",session.first.blocks[index].bytes},{"lastBytes",session.last.blocks[index].bytes},
            {"readHighWater",usage.readEnd},{"cursorPeak",usage.cursorPeak},{"reads",usage.reads},{"overflow",usage.overflow},{"invalidCursor",usage.invalidCursor}});}
    for(size_t index=0;index<profile.modelCount;++index){const auto& model=session.models[index];modelsMatch=modelsMatch&&model.matches;
        models.push_back({{"expectedHash",profile.models[index].hash},{"identity",model.identity},{"readable",model.readable},{"matches",model.matches},{"reason",model.reason},
            {"surfaces",model.surfaces},{"lods",model.lods},{"normalBones",model.normal},{"clientBones",model.clients}});}
    const bool complete=loadResult==0&&session.context&&session.listComplete&&!session.contextConflict&&!session.capacityMismatch&&!session.rangeInvalid&&!session.ambiguousRead&&!session.truncated&&ranges;
    Json evidence=Json::array();const std::array<const char*,4> kinds{"capacity-mismatch","unmapped-read","read-crossing","invalid-cursor"};
    for(size_t kind=0;kind<kinds.size();++kind)for(unsigned i=0;i<session.evidenceCounts[kind];++i){
        const auto& event=session.evidence[kind][i];
        evidence.push_back({{"kind",kinds[kind]},{"call",event.call},{"destination",event.destination},{"bytes",event.bytes},
            {"beforeValid",event.beforeValid},{"before",event.beforeValid?frameEvidence(event.before):Json(nullptr)},{"after",frameEvidence(event.after)}});
    }
    Json ledger=Json::array();for(unsigned i=0;i<session.ledgerCount;++i){const auto& item=session.readLedger[i];
        ledger.push_back({{"call",item.call},{"bytes",item.bytes},{"destination",item.destination},{"callerRva",item.callerRva},
            {"beforeValid",item.beforeValid},{"beforeCursor",item.beforeCursor},{"afterCursor",item.afterCursor},{"activeBlock",item.activeBlock},
            {"regionBase",item.regionBase},{"regionBytes",item.regionBytes},{"regionState",item.regionState},{"regionProtect",item.regionProtect}});}
    Json codeRegions=Json::array();for(unsigned i=0;i<session.codeRegionCount;++i){const auto& region=session.codeRegions[i];codeRegions.push_back({{"seedRva",region.seedRva},{"allocationBase",region.allocationBase},{"address",region.address},{"bytes",region.bytes},{"state",region.state},{"protect",region.protect}});}
    return {{"schemaVersion",1},{"evidenceVersion",3},{"codeWindowCount",session.codeFunctionCount},{"codeRejectCounts",session.codeRejects},{"codeRegions",codeRegions},{"readEvidence",evidence},{"readLedger",ledger},{"ledgerTruncated",session.ledgerTruncated},{"firstFrame",frameEvidence(session.first)},{"lastFrame",frameEvidence(session.last)},
        {"name",profile.name},{"manifestSha256",profile.manifestSha},{"pid",GetCurrentProcessId()},{"ticket",session.ticket},
        {"loadResult",loadResult},{"listResult",session.listResult},{"declaredAssets",session.declared},{"consumedAssets",session.consumed},{"listComplete",session.listComplete},
        {"contextObserved",session.context!=0},{"ownerZone",session.last.ownerZone},{"contextConflict",session.contextConflict},{"capacityMismatch",session.capacityMismatch},
        {"rangeInvalid",session.rangeInvalid},{"ambiguousRead",session.ambiguousRead},{"truncated",session.truncated},{"readCalls",session.readCalls},{"unmappedReads",session.unmappedReads},
        {"blockObservationComplete",complete},{"modelGraphsMatch",modelsMatch},{"blocks",blocks},{"models",models},
        {"gpuResidencyValidated",false},{"playerBindingValidated",false},{"gameplayReady",false},{"manualEngineCalls",0}};
}
inline bool selected(const char* filename){
    if(!enabled.load(std::memory_order_acquire)||!filename)return false;
    std::array<char,64> name{};const auto address=reinterpret_cast<uintptr_t>(filename);
    for(size_t index=0;index<name.size();++index){if(address>UINTPTR_MAX-index||!copy(address+index,&name[index],1))return false;
        if(!name[index])return (index==profile.name.size()&&std::memcmp(name.data(),profile.name.data(),index)==0)||
            (index==profile.name.size()+3&&std::memcmp(name.data(),profile.name.data(),profile.name.size())==0&&std::memcmp(name.data()+profile.name.size(),".ff",3)==0);}
    return false;
}
// Capture bounded executable windows from the first eight selected reads.
// Unwind records identify seeds, not complete C++ functions (records can be fragmented).
inline void captureReaderCode(Session& session) noexcept {
    if(session.readCalls>8)return;
    try{std::array<void*,12> stack{};const auto count=CaptureStackBackTrace(0,static_cast<DWORD>(stack.size()),stack.data(),nullptr);
        for(USHORT i=0;i<count&&session.codeFunctionCount<session.codeFunctions.size();++i){
            const auto pc=reinterpret_cast<uintptr_t>(stack[i]);if(pc<imageBase||pc-imageBase>=0x20000000)continue;
            DWORD64 image=0;const auto function=RtlLookupFunctionEntry(pc,&image,nullptr);
            if(!function||image!=imageBase||function->EndAddress<=function->BeginAddress||function->EndAddress-function->BeginAddress>16384){++session.codeRejects[0];continue;}
            if(std::find(session.codeFunctions.begin(),session.codeFunctions.begin()+session.codeFunctionCount,function->BeginAddress)!=session.codeFunctions.begin()+session.codeFunctionCount)continue;
            const auto address=imageBase+function->BeginAddress;MEMORY_BASIC_INFORMATION region{};
            if(!VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)){++session.codeRejects[1];continue;}
            if(session.codeRegionCount<session.codeRegions.size())session.codeRegions[session.codeRegionCount++]={function->BeginAddress,reinterpret_cast<uintptr_t>(region.AllocationBase),reinterpret_cast<uintptr_t>(region.BaseAddress),region.RegionSize,region.State,region.Protect};
            // The current executing stack seed plus module unwind record pins code identity.
            // Decrypted/remapped code need not retain the PE image's allocation base.
            if(region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))){++session.codeRejects[2];continue;}
            const DWORD protect=region.Protect&0xff;
            if(protect!=PAGE_EXECUTE&&protect!=PAGE_EXECUTE_READ&&protect!=PAGE_EXECUTE_READWRITE&&protect!=PAGE_EXECUTE_WRITECOPY){++session.codeRejects[3];continue;}
            const auto base=reinterpret_cast<uintptr_t>(region.BaseAddress);
            if(base>address||region.RegionSize>UINTPTR_MAX-base||address>=base+region.RegionSize){++session.codeRejects[4];continue;}
            const auto length=std::min<size_t>(8192,base+region.RegionSize-address);
            std::array<unsigned char,8192> bytes{};
            if(!copy(address,bytes.data(),length)){++session.codeRejects[5];continue;}
            const auto filename="bo7-character-window-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(function->BeginAddress)+".bin";
            std::ofstream file(reportFolder/filename,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),length);file.flush();
            if(!file){++session.codeRejects[6];continue;}
            session.codeFunctions[session.codeFunctionCount++]=function->BeginAddress;
        }
    }catch(...){}
}
__declspec(noinline) inline uintptr_t __fastcall observeRead(uintptr_t context,uintptr_t destination,uint64_t bytes){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());const uintptr_t callerRva=caller>=imageBase&&caller-imageBase<0x20000000?caller-imageBase:0;
    const auto beforeError=GetLastError();Frame before;const bool beforeValid=currentSession&&readFrame(context,before);SetLastError(beforeError);
    const auto result=originalRead(context,destination,bytes);const auto saved=GetLastError();
    if(currentSession){recordRead(*currentSession,context,destination,bytes,beforeValid?&before:nullptr,callerRva);captureReaderCode(*currentSession);}SetLastError(saved);return result;
}
inline int __fastcall observeList(uintptr_t context,uintptr_t zone,uintptr_t list,uintptr_t auxiliary,uintptr_t backend,unsigned char resume){
    const int result=originalList(context,zone,list,auxiliary,backend,resume);const auto saved=GetLastError();
    if(currentSession)recordList(*currentSession,context,list,result);SetLastError(saved);return result;
}
inline int __fastcall observeLoad(const char* filename,const void* record,uint64_t flags,uint64_t opaque,unsigned char fifth,unsigned char sixth,uint32_t seventh){
    const auto before=GetLastError();Session session;auto* previous=currentSession;currentSession=nullptr;
    if(selected(filename)){session.ticket=tickets.fetch_add(1)+1;if(session.ticket<=8)currentSession=&session;}
    SetLastError(before);const int result=originalLoad(filename,record,flags,opaque,fifth,sixth,seventh);const auto after=GetLastError();
    const bool observed=currentSession==&session;currentSession=previous;
    if(observed){try{std::ofstream file(reportFolder/("bo7-character-observation-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(session.ticket)+".json"),std::ios::binary);file<<report(session,result).dump(2)<<'\n';}catch(...){}}
    SetLastError(after);return result;
}
inline bool guardedEntry(uintptr_t base,uintptr_t rva,const unsigned char* prefix,size_t bytes){
    if(!base||base>UINTPTR_MAX-rva)return false;std::array<unsigned char,32> actual{};if(bytes>actual.size()||!copy(base+rva,actual.data(),bytes)||std::memcmp(actual.data(),prefix,bytes))return false;
    DWORD64 image=0;const auto entry=RtlLookupFunctionEntry(base+rva,&image,nullptr);
    return entry&&image==base&&entry->BeginAddress==rva&&entry->EndAddress>entry->BeginAddress&&entry->EndAddress-entry->BeginAddress<=65536;
}
inline bool prepare(uintptr_t base,const Json& manifest,const std::string& manifestSha,const std::filesystem::path& game,HashFile hashFile){
    if(enabled.load()||originalLoad||originalRead||originalList||mainLease!=INVALID_HANDLE_VALUE||!hashFile)return false;
    Profile prepared;try{prepared=parseProfile(manifest,manifestSha);}catch(...){return false;}
    const std::array<unsigned char,24> load{0x40,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xe0,0x0c,0x00,0x00,0xc5,0xf8,0x29,0xb4,0x24,0xb0,0x0c,0x00};
    const std::array<unsigned char,24> list{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20};
    const std::array<unsigned char,15> read{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    if(!guardedEntry(base,0x3a6ad20,load.data(),load.size())||!guardedEntry(base,0x3ad41b0,list.data(),list.size())||!guardedEntry(base,0x3a50310,read.data(),read.size()))return false;
    const auto file=game/(prepared.name+".ff");HANDLE lease=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(lease==INVALID_HANDLE_VALUE)return false;
    bool pinned=false;try{BY_HANDLE_FILE_INFORMATION info{};const auto& files=manifest.at("files");const auto found=std::find_if(files.begin(),files.end(),[&](const auto& entry){return entry.at("name")==prepared.name+".ff";});
        pinned=GetFileInformationByHandle(lease,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&found!=files.end()&&hashFile(file)==found->at("sha256").template get<std::string>();}catch(...){}
    if(!pinned){CloseHandle(lease);return false;}
    std::array<LPVOID,3> targets{reinterpret_cast<LPVOID>(base+0x3a6ad20),reinterpret_cast<LPVOID>(base+0x3ad41b0),reinterpret_cast<LPVOID>(base+0x3a50310)};
    std::array<LPVOID,3> callbacks{reinterpret_cast<LPVOID>(&observeLoad),reinterpret_cast<LPVOID>(&observeList),reinterpret_cast<LPVOID>(&observeRead)},originals{};
    for(size_t index=0;index<targets.size();++index){if(MH_CreateHook(targets[index],callbacks[index],&originals[index])!=MH_OK){for(size_t prior=0;prior<index;++prior)MH_RemoveHook(targets[prior]);CloseHandle(lease);return false;}}
    profile=prepared;imageBase=base;reportFolder=game/L"kronos"/L"Logs";mainLease=lease;
    originalLoad=reinterpret_cast<Load>(originals[0]);originalList=reinterpret_cast<List>(originals[1]);originalRead=reinterpret_cast<Read>(originals[2]);enabled.store(true,std::memory_order_release);return true;
}
}
