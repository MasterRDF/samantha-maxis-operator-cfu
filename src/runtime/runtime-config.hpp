#pragma once
#include "third-party/json.hpp"
#include <regex>

namespace runtime_config {
using Json=nlohmann::json;
// Game languages accepted by this runtime. A null pin means the localized global stream is measured by
// prepare-test and pinned in active-mod.json for that activation.
struct Language {const char* id;const char* prefix;const char* forceLanguage;const char* pinnedGlobalSha;};
inline constexpr std::array<Language,2> Languages{{{"french","fra_","french",FrenchSha},{"english","eng_","english",nullptr}}};
inline const Language* findLanguage(const std::string& id){for(const auto& language:Languages)if(id==language.id)return &language;return nullptr;}
struct Config {std::string activation,name,manifestSha,language,prefix,forceLanguage,localizedGlobalSha;std::filesystem::path manifest;Json document;};
inline Json read(const std::filesystem::path& path,size_t maximum=1048576){
    const auto size=std::filesystem::file_size(path);if(size==0||size>maximum)throw std::runtime_error("JSON input exceeds its size limit.");
    std::ifstream file(path,std::ios::binary);return Json::parse(file);
}
inline std::filesystem::path child(const std::filesystem::path& root,const std::string& relative){
    const auto path=std::filesystem::path(std::u8string(relative.begin(),relative.end()));
    if(path.empty()||path.is_absolute()||path.has_root_name())throw std::runtime_error("Absolute configuration path refused.");
    for(const auto& component:path)if(component==L".."||component==L".")throw std::runtime_error("Escaping configuration path refused.");
    auto full=root/path;
    for(auto walk=full;walk!=root&&walk.has_parent_path();walk=walk.parent_path()){
        const auto attributes=GetFileAttributesW(walk.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse configuration path refused.");
    }
    return full;
}
inline Config load(const std::filesystem::path& game){
    const auto folder=game/L"kronos"/L"runtime";auto active=read(folder/L"active-mod.json");
    if(active.at("schemaVersion").get<int>()!=1)throw std::runtime_error("Unsupported activation version.");
    Config result;result.activation=active.at("activationId").get<std::string>();result.manifestSha=active.at("manifestSha256").get<std::string>();
    if(!std::regex_match(result.activation,std::regex("[a-f0-9]{32}"))||!std::regex_match(result.manifestSha,std::regex("[a-f0-9]{64}")))throw std::runtime_error("Invalid activation identity.");
    result.manifest=child(folder,active.at("manifest").get<std::string>());
    if(sha256(result.manifest)!=result.manifestSha)throw std::runtime_error("The selected manifest has changed.");
    result.document=read(result.manifest);auto& manifest=result.document;
    result.name=manifest.at("name").get<std::string>();
    if(!std::regex_match(result.name,std::regex("[a-z][a-z0-9_]{0,47}")))throw std::runtime_error("Invalid package name.");
    const auto* language=manifest.at("language").is_string()?findLanguage(manifest.at("language").get<std::string>()):nullptr;
    if(manifest.at("schemaVersion").get<int>()!=1||manifest.at("runtimeMinimumVersion")!="0.1"||manifest.at("target")!="bo7-cod25-cl29269900"||
        !language||manifest.at("gameSha256")!=GameSha||manifest.at("kronosSha256")!=ClientSha||manifest.at("assetCount").get<int>()<0)
        throw std::runtime_error("Incompatible package game, language or runtime profile.");
    result.language=language->id;result.prefix=language->prefix;result.forceLanguage=language->forceLanguage;
    if(active.contains("language")&&active.at("language")!=result.language)throw std::runtime_error("The activation and its manifest name different game languages.");
    if(active.contains("localizedGlobalSha256")){
        result.localizedGlobalSha=active.at("localizedGlobalSha256").get<std::string>();
        if(!std::regex_match(result.localizedGlobalSha,std::regex("[a-f0-9]{64}")))throw std::runtime_error("Invalid localized global stream pin.");
        if(language->pinnedGlobalSha&&result.localizedGlobalSha!=language->pinnedGlobalSha)throw std::runtime_error("The activation pins an unsupported localized global stream.");
    }else if(language->pinnedGlobalSha)result.localizedGlobalSha=language->pinnedGlobalSha;
    else throw std::runtime_error("The activation does not pin the localized global stream; prepare the package again.");
    const auto& files=manifest.at("files");if(!files.is_array()||files.size()!=5)throw std::runtime_error("Exactly five fastfiles are required.");
    const std::array<std::string,5> prefixes{"","jp_",result.prefix,"ww_","srv_"};
    std::array<trusted_fastfiles::Entry,5> entries{};
    for(size_t index=0;index<prefixes.size();++index){
        const auto name=prefixes[index]+result.name+".ff";const auto found=std::find_if(files.begin(),files.end(),[&](const auto& f){return f.at("name")==name;});
        if(found==files.end()||std::count_if(files.begin(),files.end(),[&](const auto& f){return f.at("name")==name;})!=1)throw std::runtime_error("Missing or duplicate fastfile.");
        auto& entry=entries[index];entry={name,found->at("sha256").get<std::string>(),index==4,found->at("streamHeaderSha256").get<std::string>()};
        const auto bytes=found->at("bytes").get<int64_t>();
        if(!std::regex_match(entry.sha,std::regex("[0-9a-f]{64}"))||!std::regex_match(entry.streamSha,std::regex("[0-9a-f]{64}"))||found->at("server").get<bool>()!=entry.server||bytes<1||bytes>600ll*1024*1024)
            throw std::runtime_error("Invalid fastfile record.");
        const auto file=child(game,name);
        if(std::filesystem::file_size(file)!=static_cast<uint64_t>(bytes)||sha256(file)!=entry.sha)throw std::runtime_error("A selected fastfile is missing or changed: "+name);
    }
    const auto& capacities=manifest.at("memoryCapacities");if(!capacities.is_array()||capacities.size()!=16)throw std::runtime_error("Invalid memory capacity profile.");
#ifndef BO7_RELEASE
    zone_usage::ExpectedBlock10Capacity=capacities.at(10).get<uint64_t>();
#endif
    const auto& scripts=manifest.at("scripts");if(!scripts.is_array()||scripts.size()>64)throw std::runtime_error("Invalid script list.");
    if(!scripts.empty()&&read(game/L"kronos"/L"settings.json").at("server").at("disableGsc").get<bool>())throw std::runtime_error("Kronos custom script loading is disabled.");
    for(const auto& script:scripts){
        const auto name=script.at("name").get<std::string>();
        if(!std::regex_match(name,std::regex("[a-zA-Z][a-zA-Z0-9_]*\\.gsc"))||script.at("file")!="scripts/"+name)throw std::runtime_error("Invalid script path.");
        const auto file=child(game/L"kronos"/L"custom_scripts",name);
        if(std::filesystem::file_size(file)!=script.at("bytes").get<uint64_t>()||sha256(file)!=script.at("sha256").get<std::string>())throw std::runtime_error("An installed package script has changed.");
    }
    trusted_fastfiles::entries=std::move(entries);return result;
}
}

namespace runtime_status {
inline std::mutex mutex;
inline std::filesystem::path folder;
inline std::atomic<bool> engineError{false};
inline nlohmann::json value={{"schemaVersion",1},{"runtimeVersion","0.3.0"},{"activationId",""},{"state","starting"},{"assetsConfirmed",false}};
inline int64_t now(){FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER ticks{};ticks.LowPart=time.dwLowDateTime;ticks.HighPart=time.dwHighDateTime;return static_cast<int64_t>((ticks.QuadPart-116444736000000000ull)/10000);}
inline void save() noexcept{
    try{std::lock_guard lock(mutex);if(folder.empty())return;value["heartbeatUtcMs"]=now();value["pid"]=GetCurrentProcessId();
        const auto temporary=folder/(L"status-"+std::to_wstring(GetCurrentProcessId())+L".tmp");
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream<<value.dump(2);stream.flush();if(!stream)return;}
        MoveFileExW(temporary.c_str(),(folder/L"status.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }catch(...){}
}
inline void set(const std::string& state,const std::string& message) noexcept{
    try{{std::lock_guard lock(mutex);if(engineError.load()&&state!="engine-error")return;value["state"]=state;value["message"]=message;}save();}catch(...){}
}
inline void onError(uint32_t code,int level,const char* message) noexcept{
    try{engineError.store(true);{std::lock_guard lock(mutex);value["state"]="engine-error";value["message"]="The engine reported an error; further publications are stopped.";value["errorCode"]=code;value["errorLevel"]=level;value["engineMessage"]=fatal_trace::boundedText(message);}save();}catch(...){}
}
inline void keepAlive(){for(;;){save();Sleep(1000);}}
}
