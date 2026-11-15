#include "engine-context.hpp"
#include "fatal-trace.hpp"
#include "runtime-trace.hpp"
#include "zone-usage.hpp"
#include "trusted-fastfiles.hpp"
#include "runtime-config.hpp"

static unsigned checks=0;static bool corrupt=false,parsed=true,streamVerified=false;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;}
static void* expectedHeader=nullptr;static const char* expectedName=nullptr;static bool* expectedAuth=nullptr;
static bool __fastcall parser(void* header,const char* name,bool* auth,void* extra){check(header==expectedHeader&&name==expectedName&&auth==expectedAuth&&extra==reinterpret_cast<void*>(0x1234),"parser arguments changed");return parsed;}
static bool __fastcall stream(const void*,const char*){return streamVerified;}
static std::string hash(const fs::path& path){return corrupt?std::string(64,'0'):sha256(path);}
static uint32_t u32(const std::vector<unsigned char>& bytes,size_t at){uint32_t value=0;std::memcpy(&value,bytes.data()+at,4);return value;}
static void writeJson(const fs::path& file,const nlohmann::json& value){fs::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out<<value.dump(2);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 90;const auto root=fs::absolute(argv[1]);const auto activeFile=root/L"kronos"/L"runtime"/L"active-mod.json";
    auto config=runtime_config::load(root);const auto active=runtime_config::read(activeFile);
    trusted_fastfiles::root=root;trusted_fastfiles::hashFile=&hash;trusted_fastfiles::original.store(&parser);trusted_fastfiles::originalStream.store(&stream);
    fs::create_directories(root/L"kronos"/L"Logs");
    for(const auto& entry:trusted_fastfiles::entries){
        std::array<uint32_t,5> header=entry.server?std::array<uint32_t,5>{1,0xac37db14,0xc38fb1de,0xeed17cb6,0xb74bdfee}:std::array<uint32_t,5>{1,0x3579fe1b,0x629e4fd7,0x5199af3f,0x9777c8fc};
        bool authenticated=false;const auto name=entry.name;expectedHeader=header.data();expectedName=name.c_str();expectedAuth=&authenticated;
        check(trusted_fastfiles::parse(header.data(),name.c_str(),&authenticated,reinterpret_cast<void*>(0x1234))&&authenticated,"manifest file not admitted");
        check(trusted_fastfiles::matchName(fs::path(name).stem().string())==&entry,"stem name mismatch");
        check(trusted_fastfiles::matchName("\\"+name)==&entry,"engine slash name mismatch");
        check(trusted_fastfiles::matchName((root/name).string())==&entry,"absolute name mismatch");
        check(!trusted_fastfiles::matchName((root/L"outside"/name).string()),"outside folder admitted");
        std::ifstream input(root/name,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
        size_t at=232;for(uint32_t i=0;i<u32(bytes,228);++i)at+=8+u32(bytes,at+4);at+=292;
        check(at+4+0x1970<=bytes.size(),"short native auth fixture");
        check(trusted_fastfiles::verifyStream(bytes.data()+at+4,name.c_str()),"valid stream rejected");
        bytes[at+4]^=1;check(!trusted_fastfiles::verifyStream(bytes.data()+at+4,name.c_str()),"changed stream admitted");
        corrupt=true;authenticated=false;
        check(trusted_fastfiles::parse(header.data(),name.c_str(),&authenticated,reinterpret_cast<void*>(0x1234))&&!authenticated,"changed file authenticated");corrupt=false;
        parsed=false;authenticated=false;check(!trusted_fastfiles::parse(header.data(),name.c_str(),&authenticated,reinterpret_cast<void*>(0x1234))&&!authenticated,"original parse failure changed");parsed=true;
        header[1]^=1;authenticated=false;check(trusted_fastfiles::parse(header.data(),name.c_str(),&authenticated,reinterpret_cast<void*>(0x1234))&&!authenticated,"wrong platform header admitted");
    }
    check(!trusted_fastfiles::matchName("global.ff")&&!trusted_fastfiles::matchName("../"+config.name+".ff"),"unselected name admitted");
    streamVerified=true;check(trusted_fastfiles::verifyStream(nullptr,"global.ff"),"original verified result changed");streamVerified=false;
    auto bad=active;bad["manifest"]="../escape.json";writeJson(activeFile,bad);bool rejected=false;try{runtime_config::load(root);}catch(...){rejected=true;}check(rejected,"escaping activation path admitted");
    bad=active;bad["manifestSha256"]=std::string(64,'0');writeJson(activeFile,bad);rejected=false;try{runtime_config::load(root);}catch(...){rejected=true;}check(rejected,"manifest checksum mismatch admitted");writeJson(activeFile,active);
    runtime_status::folder=root/L"kronos"/L"runtime";runtime_status::value["activationId"]=config.activation;
    runtime_status::set("engine-loaded","Request state 2 only.");auto report=runtime_config::read(runtime_status::folder/L"status.json");
    check(report["state"]=="engine-loaded"&&report["assetsConfirmed"]==false&&report["activationId"]==config.activation,"incorrect status identity or asset claim");
    check(runtime_status::now()-report["heartbeatUtcMs"].get<int64_t>()<2000,"stale heartbeat");
    runtime_status::onError(0x05466e03,1,"DEV ERROR 0x05466E03");runtime_status::set("engine-loaded","Must not replace an error.");report=runtime_config::read(runtime_status::folder/L"status.json");
    check(report["state"]=="engine-error"&&report["errorCode"]==0x05466e03,"native error overwritten");
    std::printf("PASS: %u generic manifest, trust, forwarding and heartbeat/error checks; no game process used.\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
