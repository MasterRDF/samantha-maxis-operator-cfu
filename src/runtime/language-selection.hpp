#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

// force_language.txt is an optional emulator override, not a prerequisite for
// a package. The caller still validates the selected localized game fastfile.
namespace language_selection {
struct Selection { std::string language, source; };
inline bool whitespace(unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';}
inline std::string normalize(std::string bytes){
    if(bytes.size()>=3&&static_cast<unsigned char>(bytes[0])==0xef&&static_cast<unsigned char>(bytes[1])==0xbb&&static_cast<unsigned char>(bytes[2])==0xbf)bytes.erase(0,3);
    else if(bytes.size()>=2){
        const auto first=static_cast<unsigned char>(bytes[0]),second=static_cast<unsigned char>(bytes[1]);
        const bool little=first==0xff&&second==0xfe,big=first==0xfe&&second==0xff;
        if(little||big){
            if(bytes.size()%2)throw std::runtime_error("Malformed UTF-16 language override.");
            std::string decoded;
            for(size_t i=2;i<bytes.size();i+=2){
                const auto low=static_cast<unsigned char>(bytes[i+(little?0:1)]),high=static_cast<unsigned char>(bytes[i+(little?1:0)]);
                if(high||low>127)throw std::runtime_error("Language override must contain a supported language name.");
                decoded.push_back(static_cast<char>(low));
            }
            bytes=std::move(decoded);
        }
    }
    while(!bytes.empty()&&whitespace(static_cast<unsigned char>(bytes.back())))bytes.pop_back();
    size_t first=0;while(first<bytes.size()&&whitespace(static_cast<unsigned char>(bytes[first])))++first;
    bytes.erase(0,first);
    for(auto& c:bytes)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return bytes;
}
inline Selection resolve(const std::filesystem::path& game,const std::string& packageLanguage){
    const auto path=game/L"steam_settings"/L"force_language.txt";
    if(!std::filesystem::exists(path))return {packageLanguage,"package-default-no-override"};
    if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>256)throw std::runtime_error("Invalid steam_settings/force_language.txt override.");
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read steam_settings/force_language.txt override.");
    const auto value=normalize(std::string(std::istreambuf_iterator<char>(file),{}));
    if(file.bad())throw std::runtime_error("Failed reading steam_settings/force_language.txt override.");
    if(value.empty())return {packageLanguage,"package-default-empty-override"};
    if(value!=packageLanguage)throw std::runtime_error("The explicit game language override is '"+value+"' but this package targets '"+packageLanguage+"'.");
    return {value,"force-language-override"};
}
}
