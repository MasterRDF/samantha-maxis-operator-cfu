#include "client-error-profile.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace p=client_error_profile;
int main(){
 unsigned checks=0; const auto check=[&](bool ok){if(!ok)throw std::runtime_error("profile fixture failed");++checks;};
 constexpr uintptr_t base=0x10000000;
 int mutate=-1, unreadable=-1; std::vector<uintptr_t> reads;
 const auto reader=[&](uintptr_t address,void* into,size_t size){
  reads.push_back(address);int index=-1;const unsigned char* data=nullptr;size_t length=0;
  if(address==base+p::HookRva){index=0;data=p::Hook.data();length=p::Hook.size();}
  if(address==base+p::TailRva){index=1;data=p::Tail.data();length=p::Tail.size();}
  if(address==base+p::InitializerRva){index=2;data=p::Initializer.data();length=p::Initializer.size();}
  check(index>=0&&size==length);
  if(index==unreadable)return false;
  std::memcpy(into,data,size);if(index==mutate)static_cast<unsigned char*>(into)[0]^=1;return true;
 };
 check(p::matches(reader,base));
 for(int i=0;i<3;++i){
  mutate=i;check(!p::matches(reader,base));reads.clear();const auto text=p::describeMismatch(reader,base);
  check(reads.size()==3);check(text.find("matches=0")!=std::string::npos);check(text.find("actual=")!=std::string::npos);
 }
 mutate=-1;
 for(int i=0;i<3;++i){
  unreadable=i;check(!p::matches(reader,base));reads.clear();const auto text=p::describeMismatch(reader,base);
  check(reads.size()==3);check(text.find("readable=0")!=std::string::npos);
 }
 unreadable=-1;check(p::matches(reader,base));
 std::cout<<"PASS: "<<checks<<" callback profile checks; all three mutations and read failures refused.\n";
}
