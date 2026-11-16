#include "engine-context.hpp"
#include "fatal-trace.hpp"
#include "trusted-fastfiles.hpp"
#include "dropin-config.hpp"
#include "runtime-config.hpp"
#include "language-selection.hpp"

int wmain(int argc,wchar_t** argv){
    try{
        require(argc==2,"Fixture path required.");const fs::path game=argv[1];
        unsigned checks=0;const auto check=[&](bool condition){require(condition,"Drop-in contract assertion failed.");++checks;};
        const auto selection=game/L"kronos"/L"mods.json";
        const auto write=[&](const std::string& text){std::ofstream file(selection,std::ios::binary);file<<text;};
        check(maxis_dropin::enabled(game));
        write("{\"enabled\":{\"unrelated\":false}}");check(maxis_dropin::enabled(game));
        write("{\"enabled\":{\"samantha_maxis_operator_cfu\":true}}");check(maxis_dropin::enabled(game));
        write("{\"enabled\":{\"samantha_maxis_operator_cfu\":false}}");check(!maxis_dropin::enabled(game));
        write("{\"enabled\":{\"samantha_maxis_operator_cfu\":0}}");check(!maxis_dropin::enabled(game));
        write("{\"enabled\":{\"samantha_maxis_operator_cfu\":1}}");check(maxis_dropin::enabled(game));
        for(const auto text:{"not-json","[]","{\"enabled\":[]}","{\"enabled\":{\"samantha_maxis_operator_cfu\":\"false\"}}"}){
            write(text);bool refused=false;try{(void)maxis_dropin::enabled(game);}catch(const std::exception&){refused=true;}check(refused);
        }
        write("{\"enabled\":{\"samantha_maxis_operator_cfu\":true}}");
        // The fixture deliberately supplies invalid shared activation JSON. A
        // successful load proves selection comes from this mod's own folder.
        const auto config=runtime_config::load(game);
        check(config.name==maxis_dropin::Name&&config.language=="english");
        const auto language=language_selection::resolve(game,config.forceLanguage);
        check(language.language=="english"&&language.source=="package-default-no-override");
        check(config.manifest==maxis_dropin::folder(game)/L"native"/L"mod.manifest.json");
        check(trusted_fastfiles::entries.size()==5);
        const auto own=maxis_dropin::folder(game)/L"native"/L"dropin.json";
        const auto original=runtime_config::read(own);auto changed=original;changed["manifestSha256"]=std::string(64,'0');
        {std::ofstream file(own);file<<changed.dump();}
        bool refused=false;try{(void)runtime_config::load(game);}catch(const std::exception&){refused=true;}check(refused);
        {std::ofstream file(own);file<<original.dump(2);}
        const auto mod=maxis_dropin::folder(game)/L"mod.json";const auto hidden=mod.parent_path()/L"mod-hidden.json";fs::rename(mod,hidden);check(!maxis_dropin::enabled(game));fs::rename(hidden,mod);
        std::cout<<"PASS: "<<checks<<" drop-in Library/configuration checks; private files only.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
