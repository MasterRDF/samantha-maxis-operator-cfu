#include "language-selection.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)throw std::runtime_error("Private fixture path required.");
        const std::filesystem::path game=argv[1],path=game/L"steam_settings"/L"force_language.txt";
        if(std::filesystem::exists(game))throw std::runtime_error("Fixture output already exists.");
        std::filesystem::create_directories(path.parent_path());
        unsigned checks=0;
        const auto check=[&](bool ok){if(!ok)throw std::runtime_error("Language fixture assertion failed.");++checks;};
        const auto write=[&](const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(text.data(),static_cast<std::streamsize>(text.size()));};
        auto selected=language_selection::resolve(game,"english");
        check(selected.language=="english"&&selected.source=="package-default-no-override");
        for(const auto& text:{std::string(),std::string(" \r\n\t"),std::string("\xef\xbb\xbf")}){
            write(text);selected=language_selection::resolve(game,"english");check(selected.language=="english"&&selected.source=="package-default-empty-override");
        }
        for(const auto& text:{std::string("english"),std::string(" ENGLISH \r\n"),std::string("\xef\xbb\xbf" "English\r\n"),std::string("\r\nenglish\n")}){
            write(text);selected=language_selection::resolve(game,"english");check(selected.language=="english"&&selected.source=="force-language-override");
        }
        for(const bool little:{true,false}){
            std::string text=little?"\xff\xfe":"\xfe\xff";
            for(const char c:std::string("English\r\n")){text.push_back(little?c:'\0');text.push_back(little?'\0':c);}
            write(text);check(language_selection::resolve(game,"english").language=="english");
        }
        for(const auto& text:{std::string("french"),std::string("german"),std::string("english\nfrench"),std::string(257,'x'),std::string("\xff\xfeX",3)}){
            write(text);bool refused=false;try{(void)language_selection::resolve(game,"english");}catch(const std::exception&){refused=true;}check(refused);
        }
        std::filesystem::remove(path);std::filesystem::create_directory(path);
        bool refused=false;try{(void)language_selection::resolve(game,"english");}catch(const std::exception&){refused=true;}check(refused);
        std::cout<<"PASS: "<<checks<<" optional-language, BOM, encoding and explicit-override checks; private files only.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
