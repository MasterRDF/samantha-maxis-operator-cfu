#include "engine-context.hpp"
#include "fatal-trace.hpp"
#include "client-error-profile.hpp"
#include "cfu-error-profile.hpp"
#include "shared-slot.hpp"
#include "sharing-policy.hpp"
#ifndef BO7_RELEASE
#include "runtime-trace.hpp"
#include "zone-usage.hpp"
#include "ncs-types-probe.hpp"
#endif
#include "trusted-fastfiles.hpp"
#include "runtime-config.hpp"
#include "minhook/include/MinHook.h"
#ifdef BO7_CHARACTER_DIAGNOSTIC
#include "character-observation.hpp"
#endif

#include <array>

static HMODULE client=nullptr;
static fs::path gameRoot;
// File whose hash identifies the Kronos client: the renamed copy (XInput bootstrap) or the loaded module itself (launcher attach).
static fs::path clientFile;
static bool attached=false;
static std::atomic<bool> started{false};
static bool pinnedCfuCompanion(){
    const auto expected=gameRoot/L"kronos"/L"mods"/L"carry_forward_unleashed"/L"mwcf_operators.dll";
    const auto module=GetModuleHandleW(L"mwcf_operators.dll");
    if(!module)return false;
    std::array<wchar_t,32768> path{};
    const auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    return count&&count<path.size()&&_wcsicmp(path.data(),expected.c_str())==0&&
        sha256(expected)=="3d692ab79d38bd3b69979bdc2a0a2a0d6c5bf51ecc4353ad5f93f179999f8957";
}
static std::mutex logMutex;
static std::ofstream logFile;
static void logLine(const std::string& text) noexcept{
    try{std::lock_guard lock(logMutex);if(logFile){logFile<<text<<'\n';logFile.flush();}}catch(...){}
}
static bool carryForwardSelected(){
    try{const auto settings=runtime_config::read(gameRoot/L"kronos"/L"settings.json");return settings.at("game").at("carryForward").get<bool>();}
    catch(...){fail("A valid explicit Carry-forward setting is required.");}
}
static void installObservers(const Memory& memory,Address base,Address clientBase,[[maybe_unused]] const runtime_config::Config& config){
    const auto note=[](const std::string& text){logLine(text);};
        const auto reader=[](Address address,void* output,size_t count){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,count,&got)&&got==count;};
        const bool stockCallback=client_error_profile::matches(reader,clientBase);
        const bool cfuCallback=!stockCallback&&pinnedCfuCompanion()&&cfu_error_profile::matches(reader,clientBase);
        if(!stockCallback&&!cfuCallback){
            note("Callback module file: "+clientFile.string());
            note(client_error_profile::describeMismatch(reader,clientBase));
            fail("Kronos v1.41 callback profile does not match.");
        }
        note(cfuCallback?"Callback profile: CFU stable v1.41 guard verified; guard preserved.":"Callback profile: stock Kronos v1.41.");
        fatal_trace::errorNotification=&runtime_status::onError;
        require(fatal_trace::install(reinterpret_cast<void* volatile*>(clientBase+client_error_profile::CellRva),memory.get<fatal_trace::Function>(clientBase+client_error_profile::CellRva),base,gameRoot/L"kronos"/L"Logs"),"Native error observer could not be installed.");
        require(memory.get<std::array<unsigned char,9>>(base+0x3a4ba70)==std::array<unsigned char,9>{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55},"Unexpected TAFA parser entry.");
        require(memory.get<std::array<unsigned char,8>>(base+0x3a484e0)==std::array<unsigned char,8>{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57},"Unexpected stream verifier entry.");
        require(MH_Initialize()==MH_OK,"Runtime hook initialization failed.");
        LPVOID parser=nullptr,stream=nullptr;
        require(MH_CreateHook(reinterpret_cast<LPVOID>(base+0x3a4ba70),reinterpret_cast<LPVOID>(&trusted_fastfiles::parse),&parser)==MH_OK&&parser,"TAFA observer preparation failed.");
        require(MH_CreateHook(reinterpret_cast<LPVOID>(base+0x3a484e0),reinterpret_cast<LPVOID>(&trusted_fastfiles::verifyStream),&stream)==MH_OK&&stream,"Stream observer preparation failed.");
        trusted_fastfiles::root=gameRoot;trusted_fastfiles::hashFile=&sha256;trusted_fastfiles::original.store(reinterpret_cast<trusted_fastfiles::Parser>(parser));trusted_fastfiles::originalStream.store(reinterpret_cast<trusted_fastfiles::StreamVerifier>(stream));
#ifndef BO7_RELEASE
        require(runtime_trace::prepare(base,gameRoot/L"kronos"/L"Logs",&fatal_trace::modRequestActive),"Independent diagnostic preparation failed.");
#ifdef BO7_CHARACTER_DIAGNOSTIC
        require(character_observation::prepare(base,config.document,config.manifestSha,gameRoot,&sha256),"Character observation profile mismatch.");
#else
        require(zone_usage::prepare(base),"Native asset/read observation profile mismatch.");
#endif
        require(ncs_types_probe::write(base,gameRoot/L"kronos"/L"Logs"),"Native NCS table profile mismatch.");
#endif
        require(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"Runtime observer activation failed.");
}
static bool frontEndReady(const Table& t){
    for(size_t i=0;i<t.size();++i){
        if(i==ExtraSlot||t[i][0x20]!=2||field<Address>(t[i],0x1b0)==0)continue;
        for(size_t n=0;n<3;++n){const auto name=nameAt(t[i],n);if(name=="frontend_ui"||name=="ingame_zm"||name=="ingame_mp")return true;}
    }
    return false;
}
static std::string overview(const Table& t,const Layout& layout,Address page){
    std::ostringstream out;
    for(size_t i=0;i<t.size();++i){
        const auto owner=field<Address>(t[i],0x1b0);std::string names;
        for(size_t n=0;n<3;++n){const auto name=nameAt(t[i],n);if(!name.empty())names+=(names.empty()?"":"|")+name;}
        if(names.empty()&&owner==0)continue;
        out<<' '<<i<<":["<<names<<"] s"<<unsigned(t[i][0x20])<<'.'<<unsigned(t[i][0x21]);
        if(i==ExtraSlot)out<<(owner==0?" owner=none":owner==layout.descriptor?" owner=game":owner==shared_slot::privateDescriptor(page)?" owner=shared":" owner=other");
    }
    return out.str();
}
// Carry-forward on: Kronos owns request 4. The package is added to each of Kronos's publications.
static void shareWithCarryForward(const Memory& memory,const Layout& layout,Address base,Address clientBase,bool allowFrontEnd,const runtime_config::Config& config){
    {std::lock_guard lock(runtime_status::mutex);runtime_status::value["mode"]="carry-forward-passive";runtime_status::value["carryForwardPreload"]=allowFrontEnd;}
    runtime_status::set("waiting-zombies","Carry-forward is on: the package will be added to Kronos's carry-forward request (request 4). Waiting for the front end.");
    logLine("Carry-forward is on in kronos/settings.json: sharing Kronos's request 4 instead of publishing alone.");
    const auto until=GetTickCount64()+30*60*1000;
    bool ready=false;
    // Ready when the front end is loaded and Kronos has installed its error callback (the observer below chains on it).
    while(GetTickCount64()<until){try{validateDescriptor(memory,layout);if(frontEndReady(snapshot(memory,layout))&&memory.get<Address>(clientBase+client_error_profile::CellRva)!=0){ready=true;break;}}catch(const std::exception&){}runtime_status::save();Sleep(500);}
    require(ready,"Timed out waiting for the front end.");
    const auto page=reinterpret_cast<Address>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(page!=0,"Name page allocation failed.");
    memory.write(page,ZoneName.c_str(),ZoneName.size()+1);
    shared_slot::install(client,layout,page,ZoneName,&runtime_status::engineError,&fatal_trace::modRequestActive,&logLine,allowFrontEnd);
    installObservers(memory,base,clientBase,config);
    shared_slot::hook.armed.store(true);
    logLine(std::string("Passive sharing armed; forced carry-forward unloading and republishing are disabled. Front-end preload opt-in: ")+(allowFrontEnd?"on.":"off."));
    shared_slot::Leftover leftover;
    std::string previous,previousOverview,lastState,lastMessage;uint64_t lastHeartbeat=0;unsigned overviewLines=0,unreadable=0;bool errorNoted=false;
    const auto status=[&](const char* state,const std::string& message){if(lastState!=state||lastMessage!=message){lastState=state;lastMessage=message;runtime_status::set(state,message);}};
    for(;;){
        if(runtime_status::engineError.load()&&!errorNoted){errorNoted=true;logLine("The engine reported an error (see status.json); the package is no longer added to new requests.");}
        Table table{};Address cell=0;
        try{validateDescriptor(memory,layout);table=snapshot(memory,layout);cell=memory.get<Address>(layout.nameCell);unreadable=0;}
        catch(const std::exception& error){if(++unreadable==1)logLine(std::string("Request table check failed: ")+error.what());runtime_status::save();Sleep(500);continue;}
        const auto& request=table[ExtraSlot];
        const auto owner=field<Address>(request,0x1b0);const auto privateOwner=shared_slot::privateDescriptor(page);
        const auto primary=nameAt(request,0),secondary=nameAt(request,1);
        unsigned publications=0;{std::lock_guard lock(shared_slot::hook.mutex);publications=shared_slot::hook.publications;}
        // The engine keeps its own working copy of the three names at +0x2a; the requested names are at +0xea.
        const auto working=[&](size_t index){const auto p=reinterpret_cast<const char*>(request.data()+0x2a+index*0x40);return std::string(p,strnlen_s(p,0x40));};
        std::ostringstream detail;detail<<"request="<<unsigned(request[0x20])<<'.'<<unsigned(request[0x21])<<" name=["<<primary<<"] names1=["<<secondary<<"] names2=["<<nameAt(request,2)
            <<"] engine=["<<working(0)<<'|'<<working(1)<<'|'<<working(2)<<"] flags="<<field<uint32_t>(request,0x24)
            <<" publications="<<publications<<" owner="<<(owner==0?"none":owner==layout.descriptor?"game":owner==privateOwner?"shared":"other")<<" cell="<<(cell==0?"none":"set");
        if(detail.str()!=previous){previous=detail.str();logLine(previous);
            std::lock_guard lock(runtime_status::mutex);runtime_status::value["publications"]=publications;runtime_status::value["requestStateLow"]=request[0x20];runtime_status::value["requestStateHigh"]=request[0x21];
            runtime_status::value["descriptorOwner"]=owner==0?"none":owner==privateOwner?"runtime":"other";runtime_status::value["nameCellOwner"]=cell==0?"none":"other";runtime_status::value["sharedWith"]=primary;}
        const bool mine=owner==privateOwner&&secondary==ZoneName&&!primary.empty();
        if(shared_slot::loaded(request,page,ZoneName))status("engine-loaded","Native request reached state 2.0 and both working names match. Asset usability still requires an in-game check.");
        else if(mine)status("publishing","The package was added to Kronos's request beside "+primary+"; assets are not confirmed.");
        else if(!primary.empty())status("waiting-zombies",primary+" is requested without the package. It is left untouched; waiting for a safe natural publication.");
        else if(publications)status("unloaded","The engine cleared the shared request. The package returns with Kronos's next carry-forward request.");
        else status("waiting-zombies","Sharing armed; waiting for Kronos to request its carry-forward pack.");
        std::string cleaned;
        {std::lock_guard lock(shared_slot::hook.mutex);const auto tick=GetTickCount64();
            try{cleaned=shared_slot::tidy(memory,layout,page,ZoneName,leftover,tick);}catch(const std::exception& error){cleaned=std::string("Leftover check failed: ")+error.what();leftover.since=0;}
        }
        if(!cleaned.empty())logLine(cleaned);
        if(overviewLines<400){const auto now=overview(table,layout,page);if(now!=previousOverview){previousOverview=now;++overviewLines;logLine("requests:"+now);}}
        const auto now=GetTickCount64();
        if(now-lastHeartbeat>=1000){runtime_status::save();lastHeartbeat=now;}Sleep(250);
    }
}
static DWORD WINAPI worker(void*){
    Handle lifetimeLock,activationLock;
    auto note=[](const std::string& text){logLine(text);};
    try{
        const auto folder=gameRoot/L"kronos"/L"runtime";fs::create_directories(folder);runtime_status::folder=folder;
        lifetimeLock.h=CreateFileW((folder/L"runtime.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lifetimeLock.h==INVALID_HANDLE_VALUE)return ERROR_SHARING_VIOLATION;
        fs::create_directories(gameRoot/L"kronos"/L"Logs");{std::lock_guard lock(logMutex);logFile.open(gameRoot/L"kronos"/L"Logs"/L"bo7-runtime-loader.log",std::ios::trunc);}
        note(std::string("Maxis runtime v0.1 with passive Carry-forward sharing; entry: ")+(attached?"launcher attach (original Kronos XINPUT9_1_0.dll)":"XInput bootstrap")+"; package selection and game language remain manifest-driven.");
        require(sha256(gameRoot/L"cod25-cod.exe")==GameSha,"Unsupported game executable.");
        require(sha256(clientFile)==ClientSha,"Unsupported original Kronos client.");
        require(sha256(gameRoot/L"global_stream_mp.ff")==GlobalSha,"The original global fastfile is required.");
        std::ifstream language(gameRoot/L"steam_settings"/L"force_language.txt");std::string lang;std::getline(language,lang);lang.erase(std::remove(lang.begin(),lang.end(),'\r'),lang.end());
        while(!lang.empty()&&(lang.back()==' '||lang.back()=='\t'))lang.pop_back();
        require(runtime_config::findLanguage(lang)!=nullptr,"Unsupported game language in steam_settings/force_language.txt: "+lang);
        if(!fs::exists(folder/L"active-mod.json")){runtime_status::set("inactive","No package is selected.");runtime_status::keepAlive();}
        activationLock.h=CreateFileW((folder/L"active-mod.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(activationLock.h!=INVALID_HANDLE_VALUE,"The selected activation cannot be locked.");
        {const auto active=runtime_config::read(folder/L"active-mod.json");std::lock_guard lock(runtime_status::mutex);runtime_status::value["activationId"]=active.at("activationId");}
        runtime_status::set("verifying","Checking the selected manifest and installed package files.");
        const auto config=runtime_config::load(gameRoot);ZoneName=config.name;
        require(lang==config.forceLanguage,"The game runs in '"+lang+"' but the selected package targets '"+config.forceLanguage+"' (steam_settings/force_language.txt).");
        require(sha256(gameRoot/fs::path(config.prefix+"global_stream_mp.ff"))==config.localizedGlobalSha,"The "+config.prefix+"global_stream_mp.ff fastfile differs from the one measured when the package was prepared.");
        note("Game language: "+config.language+" ("+config.prefix+" companion).");
        {std::lock_guard lock(runtime_status::mutex);runtime_status::value["activationId"]=config.activation;runtime_status::value["name"]=config.name;runtime_status::value["manifestSha256"]=config.manifestSha;runtime_status::value["language"]=config.language;}
        runtime_status::set("waiting-zombies","Manifest and installed files verified; waiting for ingame_zm state 2.");
        const Address base=reinterpret_cast<Address>(GetModuleHandleW(nullptr)),clientBase=reinterpret_cast<Address>(client);
        Memory memory{GetCurrentProcess()};Layout layout{base+RequestsRva,base+DescriptorRva,base+NameCellRva};
        if(carryForwardSelected()){shareWithCarryForward(memory,layout,base,clientBase,sharing_policy::preloadAllowed(config.document),config);runtime_status::keepAlive();}
        const auto until=GetTickCount64()+30*60*1000;
        bool ready=false;
        while(GetTickCount64()<until){try{validateDescriptor(memory,layout);if(zombieReady(snapshot(memory,layout))){ready=true;break;}}catch(const std::exception&){}runtime_status::save();Sleep(1000);}
        require(ready,"Timed out waiting for the native Zombies request.");preconditions(memory,layout);Sleep(100);preconditions(memory,layout);
        installObservers(memory,base,clientBase,config);
        const auto page=reinterpret_cast<Address>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(page!=0,"Name page allocation failed.");
        Installation initial{page};memory.write(page,ZoneName.c_str(),ZoneName.size()+1);fatal_trace::modRequestActive.store(true);
        try{publish(memory,layout,initial);}catch(...){if(!initial.committed)rollbackUnpublished(memory,layout,initial);throw;}
        runtime_status::set("publishing","Native package request published; assets are not confirmed.");note("Published package: "+ZoneName);
        request_lifecycle::Policy lifecycle;std::string previous;uint64_t lastHeartbeat=0;
#ifndef BO7_RELEASE
        unsigned clearedDumpPublication=0;
#endif
        for(;;){
            if(runtime_status::engineError.load()){runtime_status::keepAlive();}
            validateDescriptor(memory,layout);const auto table=snapshot(memory,layout);const auto& request=table[ExtraSlot];const auto cell=memory.get<Address>(layout.nameCell);
            const bool zombies=zombieReady(table),idle=idleOwnedSlot(request,layout.descriptor);
            const auto state=int(request[0x20])+(int(request[0x21])<<8);const auto name=nameAt(request,0);
            const auto owner=field<Address>(request,0x1b0);
            const bool dirtyNames=std::any_of(request.begin()+0xea,request.begin()+0x1aa,[](auto value){return value!=0;});
            std::ostringstream detail;detail<<"request="<<unsigned(request[0x20])<<'.'<<unsigned(request[0x21])<<" name=["<<name<<"] zombies="<<zombies<<" idle="<<idle<<" publications="<<lifecycle.publications
                <<" owner=0x"<<std::hex<<owner<<" cell=0x"<<cell<<std::dec<<" names1=["<<nameAt(request,1)<<"] names2=["<<nameAt(request,2)<<"] dirtyNameStorage="<<dirtyNames;
            if(detail.str()!=previous){previous=detail.str();note(previous);
                {std::lock_guard lock(runtime_status::mutex);runtime_status::value["publications"]=lifecycle.publications;runtime_status::value["requestStateLow"]=request[0x20];runtime_status::value["requestStateHigh"]=request[0x21];runtime_status::value["idleEligible"]=idle;runtime_status::value["dirtyNameStorage"]=dirtyNames;runtime_status::value["descriptorOwner"]=owner==0?"none":owner==layout.descriptor?"runtime":"other";runtime_status::value["nameCellOwner"]=cell==0?"none":cell==page?"runtime":"other";}
                if(state==2&&name==ZoneName&&field<Address>(request,0x1b0)==layout.descriptor)runtime_status::set("engine-loaded","Native request reached state 2.0. Asset usability still requires an in-game check.");
                else if(name.empty())runtime_status::set("unloaded","The engine cleared the package request. The original ownership guards remain active.");
            }
#ifndef BO7_RELEASE
            if(name.empty()&&clearedDumpPublication!=lifecycle.publications){clearedDumpPublication=lifecycle.publications;std::ofstream dump(gameRoot/L"kronos"/L"Logs"/("bo7-runtime-cleared-request-"+std::to_string(lifecycle.publications)+".bin"),std::ios::binary|std::ios::trunc);dump.write(reinterpret_cast<const char*>(request.data()),request.size());}
#endif
            if((owner!=0&&owner!=layout.descriptor)||(!name.empty()&&name!=ZoneName)){runtime_status::set("refused","The native request slot belongs to another package; no further publication.");runtime_status::keepAlive();}
            const auto now=GetTickCount64();
            const auto decision=lifecycle.poll(zombies,idle,cell,page,now,requestFingerprint(request,cell));
            if(decision==request_lifecycle::Decision::foreignOwner||decision==request_lifecycle::Decision::limitReached){runtime_status::set("refused","Request ownership changed or the publication limit was reached; no further publication.");runtime_status::keepAlive();}
            if(decision==request_lifecycle::Decision::republish){if(runtime_status::engineError.load())runtime_status::keepAlive();Installation next{page};next.republishing=true;
                try{publish(memory,layout,next);lifecycle.published();runtime_status::set("publishing","Republished after a stable native transition.");}
                catch(const std::exception& error){if(!next.committed)rollbackUnpublished(memory,layout,next);lifecycle.retryLater();note(std::string("Publication deferred: ")+error.what());}}
            if(now-lastHeartbeat>=1000){runtime_status::save();lastHeartbeat=now;}Sleep(250);
        }
    }catch(const std::exception& error){note(std::string("Refused: ")+error.what());runtime_status::set("refused",error.what());runtime_status::keepAlive();}
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI Bo7RuntimeStart(HMODULE originalClient,const wchar_t* directory){
    if(!originalClient||!directory||started.exchange(true))return ERROR_INVALID_PARAMETER;
    client=originalClient;gameRoot=fs::path(directory);clientFile=gameRoot/L"bo7-runtime-original.dll";
    const auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return GetLastError();CloseHandle(thread);return ERROR_SUCCESS;
}
// Launcher entry: called on a new thread after bo7-linker attach has loaded this DLL into BO7.
// It only starts when the game's own XINPUT9_1_0.dll (the original Kronos client) is already loaded;
// the worker still verifies its hash, the game files and the selected activation before doing anything.
extern "C" __declspec(dllexport) DWORD WINAPI Bo7RuntimeAttach(void*){
    const HMODULE kronos=GetModuleHandleW(L"XINPUT9_1_0.dll");if(!kronos)return ERROR_MOD_NOT_FOUND;
    std::array<wchar_t,32768> exe{},module{};
    if(!GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()))||!GetModuleFileNameW(kronos,module.data(),static_cast<DWORD>(module.size())))return GetLastError();
    if(started.exchange(true))return ERROR_ALREADY_INITIALIZED;
    client=kronos;gameRoot=fs::path(exe.data()).parent_path();clientFile=fs::path(module.data());attached=true;
    const auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!thread)return GetLastError();CloseHandle(thread);return ERROR_SUCCESS;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
