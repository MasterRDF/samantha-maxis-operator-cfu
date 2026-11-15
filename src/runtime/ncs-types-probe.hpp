#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace ncs_types_probe {
struct Info { uint64_t precache,name;uint32_t type,pad;uint64_t category;uint32_t unknown;uint8_t flag,padding[3]; };
static_assert(sizeof(Info)==40);
inline bool read(uintptr_t at,void* out,size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;}
inline std::string text(uint64_t at){if(!at)return {};std::string value;for(size_t i=0;i<128;i++){char c=0;if(!read(static_cast<uintptr_t>(at)+i,&c,1))return {};if(c==0)return value;if(c<32||static_cast<unsigned char>(c)>126)return {};value+=c;}return {};}
// CL29269900 labels witnessed in v0.18; module-relative RVA confirmed in
// v0.19 (PID 1588). Allocation bases of split code mappings are not PE bases.
// The relay independently pins the executable SHA before calling this probe.
inline constexpr uintptr_t ProfileTableRva=0x1231b510;
inline bool entry(const Info& e,const char* precache,const char* name,uint32_t type){return e.type==type&&text(e.precache)==precache&&text(e.name)==name;}
inline bool anchors(const std::array<Info,90>& e){return entry(e[0],"precache_xmodel","mdl",8)&&entry(e[1],"precache_material","mat",10)&&entry(e[2],"precache_image","img",14);}
inline bool weapon(const Info& e){return entry(e,"","wep",57);}
inline bool fx(const Info& e){return entry(e,"precache_particlesystem","particlesystem",58);}
inline bool matches(const unsigned char* p){const std::array<int,18> signature{0x48,0x89,0x05,-1,-1,-1,-1,0xc6,0x45,0x10,0x00,0xe8,-1,-1,-1,-1,0x33,0xff};for(size_t i=0;i<signature.size();i++)if(signature[i]>=0&&p[i]!=signature[i])return false;return true;}
struct Sample { uintptr_t table=0;std::array<std::string,4> precache{},names{};std::array<uint32_t,4> types{}; };
struct Result {
    bool found=false,fxTypeSix=false;uintptr_t table=0;std::array<Info,90> entries{};std::string reason,method;
    size_t signatureMatches=0,readableCandidates=0,readFailures=0,modelStringCount=0;std::vector<Sample> samples;
};
struct Section { uintptr_t start=0;size_t length=0;bool execute=false,readable=false; };
inline Result inspect(uintptr_t image,uintptr_t profileRva=ProfileTableRva){
    Result result;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!read(image,&dos,sizeof dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000||!read(image+dos.e_lfanew,&nt,sizeof nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections==0||nt.FileHeader.NumberOfSections>96){result.reason="invalid-image";return result;}
    const size_t size=nt.OptionalHeader.SizeOfImage;if(size<4096||size>1024ull*1024*1024){result.reason="invalid-image-size";return result;}
    const uintptr_t sectionAt=image+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    std::vector<Section> sections;for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++){
        IMAGE_SECTION_HEADER s{};if(!read(sectionAt+i*sizeof s,&s,sizeof s)||s.VirtualAddress>=size)continue;
        const size_t length=std::min<size_t>(std::max(s.Misc.VirtualSize,s.SizeOfRawData),size-s.VirtualAddress);
        if(length)sections.push_back({image+s.VirtualAddress,length,(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0,(s.Characteristics&IMAGE_SCN_MEM_READ)!=0});
    }
    std::vector<unsigned char> buffer(65536);std::unordered_set<uintptr_t> accepted,inspected;
    const auto candidate=[&](uintptr_t table){
        if(table<image||table>image+size-sizeof result.entries||!inspected.insert(table).second)return;
        std::array<Info,90> entries{};if(!read(table,entries.data(),sizeof entries)){result.readFailures++;return;}result.readableCandidates++;
        if(result.samples.size()<8){Sample sample;sample.table=table;const std::array<size_t,4> ids{0,1,2,6};for(size_t k=0;k<ids.size();k++){sample.precache[k]=text(entries[ids[k]].precache);sample.names[k]=text(entries[ids[k]].name);sample.types[k]=entries[ids[k]].type;}result.samples.push_back(sample);}
        // Exact labels AND native type IDs distinguish this metadata from the
        // asset-name table, WORLD/MODEL enum strings and eight-byte aliases.
        if(!anchors(entries))return;
        accepted.insert(table);result.table=table;result.entries=entries;
    };
    const auto finish=[&](){
        if(accepted.size()!=1){result.found=false;result.reason=accepted.empty()?"table-not-found":"ambiguous-table";return result;}
        result.found=true;result.fxTypeSix=fx(result.entries[6]);result.reason=result.fxTypeSix?"verified-model-and-fx-indices":"fx-index-mismatch";return result;
    };
    // ASLR-safe, build-specific fast path. Never trust the address alone. If
    // the anchors match but FX differs, refuse without searching another table.
    if(profileRva<size){candidate(image+profileRva);if(!accepted.empty()){result.method="validated-profile-rva";return finish();}}
    for(const auto& s:sections)if(s.execute){
        for(size_t offset=0;offset<s.length;offset+=buffer.size()-17){const size_t n=std::min(buffer.size(),s.length-offset);const uintptr_t at=s.start+offset;
            if(!read(at,buffer.data(),n)){result.readFailures++;continue;}
            for(size_t i=0;i+18<=n;i++)if(matches(buffer.data()+i)){result.signatureMatches++;int32_t relative=0;std::memcpy(&relative,buffer.data()+i+3,4);candidate(at+i+7+relative);}
        }
    }
    result.method="code-signature";
    if(accepted.empty()){
        // Locate the same structure through in-module precache_xmodel/mdl pointers
        // if the initializer's instruction sequence changed.
        result.method="data-table-scan";std::unordered_set<uintptr_t> modelStrings;
        for(const auto& s:sections)if(s.readable&&!s.execute){
            for(size_t offset=0;offset<s.length;offset+=buffer.size()-15){const size_t n=std::min(buffer.size(),s.length-offset);const uintptr_t at=s.start+offset;
                if(!read(at,buffer.data(),n)){result.readFailures++;continue;}
                // Compilers may share a literal with the suffix of another one.
                for(size_t i=0;i<n;i++)if(buffer[i]=='p'||buffer[i]=='m'){
                    for(const char* name:{"precache_xmodel","mdl"}){const size_t length=std::strlen(name)+1;if(length<=n-i&&std::memcmp(buffer.data()+i,name,length)==0)modelStrings.insert(at+i);}
                }
            }
        }
        result.modelStringCount=modelStrings.size();
        if(!modelStrings.empty())for(const auto& s:sections)if(s.readable&&!s.execute){
            for(size_t offset=0;offset<s.length;offset+=buffer.size()-7){const size_t n=std::min(buffer.size(),s.length-offset);const uintptr_t at=s.start+offset;
                if(!read(at,buffer.data(),n)){result.readFailures++;continue;}
                for(size_t i=static_cast<size_t>((8-(at&7))&7);i+8<=n;i+=8){uint64_t pointer=0;std::memcpy(&pointer,buffer.data()+i,8);if(modelStrings.contains(static_cast<uintptr_t>(pointer))){candidate(at+i);if(at+i>=image+8)candidate(at+i-8);}}
            }
        }
        // The labels can also be runtime allocations outside the image. In
        // that case recognize aligned metadata records before reading labels.
        const auto canonical=[](uint64_t p){return p>=0x10000&&p<0x800000000000ull;};
        const auto record=[&](const Info& e){return (e.precache||e.name)&&(!e.precache||canonical(e.precache))&&(!e.name||canonical(e.name))&&e.pad==0&&e.flag<=1;};
        const size_t beforeTyped=accepted.size();
        for(const auto& s:sections)if(s.readable&&!s.execute){
            for(size_t offset=0;offset<s.length;offset+=buffer.size()-79){const size_t n=std::min(buffer.size(),s.length-offset);const uintptr_t at=s.start+offset;
                if(!read(at,buffer.data(),n)){result.readFailures++;continue;}
                for(size_t i=static_cast<size_t>((8-(at&7))&7);i+2*sizeof(Info)<=n;i+=8){Info first{},second{};std::memcpy(&first,buffer.data()+i,sizeof first);if(first.type!=8||!record(first))continue;std::memcpy(&second,buffer.data()+i+sizeof first,sizeof second);if(second.type==10&&record(second))candidate(at+i);}
            }
        }
        if(accepted.size()>beforeTyped)result.method="typed-data-table-scan";
    }
    return finish();
}
inline std::string quoted(const std::string& input){std::string value="\"";for(char c:input){if(c=='\"')value+='\"';value+=c;}return value+'\"';}
inline bool write(uintptr_t image,const std::filesystem::path& folder){
    const auto r=inspect(image);std::ofstream log(folder/L"bo7-runtime-ncs-types.log",std::ios::trunc);
    log<<"Read-only native NCS profile v0.1: "<<r.reason<<"; found="<<r.found<<"; fxTypeSix="<<r.fxTypeSix<<"\n";
    log<<"image=0x"<<std::hex<<image<<" expectedTableRva=0x"<<ProfileTableRva;if(r.found)log<<" table=0x"<<r.table<<" tableRva=0x"<<(r.table-image);log<<std::dec<<"\n";
    log<<"method="<<r.method<<" signatureMatches="<<r.signatureMatches<<" readableCandidates="<<r.readableCandidates<<" readFailures="<<r.readFailures<<" modelStrings="<<r.modelStringCount<<"\n";
    for(const auto& sample:r.samples){log<<"candidate=0x"<<std::hex<<sample.table<<std::dec;const std::array<size_t,4> ids{0,1,2,6};for(size_t k=0;k<ids.size();k++)log<<" ["<<ids[k]<<":"<<quoted(sample.precache[k])<<"/"<<quoted(sample.names[k])<<" type="<<sample.types[k]<<"]";log<<"\n";}
    if(r.found){log<<"id,precache,name,assetType,category\n";for(size_t i=0;i<r.entries.size();i++){const auto& e=r.entries[i];log<<i<<","<<quoted(text(e.precache))<<","<<quoted(text(e.name))<<","<<e.type<<","<<quoted(text(e.category))<<"\n";}}
    log<<"weaponTypeFourteen="<<(r.found&&weapon(r.entries[14]))<<"\n";
    return r.found&&r.fxTypeSix&&weapon(r.entries[14]);
}
}
