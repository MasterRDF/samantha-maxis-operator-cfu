#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "third-party/json.hpp"

namespace maxis_dropin {
inline constexpr char Name[]="samantha_maxis_operator_cfu";
inline std::filesystem::path folder(const std::filesystem::path& game){
    return game/L"kronos"/L"mods"/Name;
}
inline bool enabled(const std::filesystem::path& game){
    if(!std::filesystem::is_regular_file(folder(game)/L"mod.json"))return false;
    const auto path=game/L"kronos"/L"mods.json";
    if(!std::filesystem::exists(path))return true;
    const auto bytes=std::filesystem::file_size(path);
    if(bytes==0||bytes>1048576)throw std::runtime_error("Invalid Kronos mod selection size.");
    std::ifstream file(path,std::ios::binary);
    const auto document=nlohmann::json::parse(file);
    if(!document.is_object())throw std::runtime_error("Invalid Kronos mod selection.");
    const auto entries=document.find("enabled");
    if(entries==document.end())return true;
    if(!entries->is_object())throw std::runtime_error("Invalid enabled-mod table.");
    const auto value=entries->find(Name);
    if(value==entries->end())return true;
    if(value->is_boolean())return value->get<bool>();
    if(value->is_number_integer()&&(*value==0||*value==1))return *value==1;
    throw std::runtime_error("Maxis Library switch must be a boolean.");
}
}
