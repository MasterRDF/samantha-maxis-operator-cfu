#include "engine-context.hpp"
void test(bool value,const char* message){require(value,std::string("SELF-TEST: ")+message);}
void selfTest() {
    // Real Read/WriteProcessMemory on private allocations in this tool, never on a game.
    Table data{}; std::array<unsigned char, 0x40> descriptor{}; Address cell = 0;
    Memory m{GetCurrentProcess()};
    Layout l{reinterpret_cast<Address>(&data), reinterpret_cast<Address>(descriptor.data()), reinterpret_cast<Address>(&cell)};
    m.put<uint64_t>(l.descriptor, DescriptorHash); m.put<uint32_t>(l.descriptor + 8, 20);
    m.put<uint32_t>(l.descriptor + 12, 1); m.put<Address>(l.descriptor + 16, l.nameCell);
    data[8][0x20] = 2; setField<Address>(data[8], 0x1b0, l.descriptor);
    std::memcpy(data[8].data() + 0xea, "ingame_zm", 10);
    auto baseline = data;
    preconditions(m, l);
    Installation in{reinterpret_cast<Address>(ZoneName.c_str())};
    publish(m, l, in);
    test(cell == in.allocation, "name pointer");
    test(nameAt(data[4], 0) == ZoneName && nameAt(data[4], 1).empty() && nameAt(data[4], 2).empty(), "queued name");
    test(field<Address>(data[4], 0x1b0) == l.descriptor, "descriptor pointer");
    auto expected = baseline;
    setField<Address>(expected[4], 0x1b0, l.descriptor); setField<uint32_t>(expected[4], 0x24, 0);
    std::memcpy(expected[4].data() + 0xea, ZoneName.c_str(), (ZoneName.size()+1));
    test(data == expected, "only intended fields changed");
    bool rejected = false; try { preconditions(m, l); } catch (...) { rejected = true; }
    test(rejected, "duplicate load rejected");
    data = baseline; cell = 0;
    data[4][0xea] = 'x'; rejected = false;
    try { preconditions(m, l); } catch (...) { rejected = true; }
    test(rejected && cell == 0, "occupied queue rejected without writing");
    data = baseline; data[8][0x20] = 0; rejected = false;
    try { preconditions(m, l); } catch (...) { rejected = true; }
    test(rejected && cell == 0, "Zombies state required");
    data = baseline; m.put<uint32_t>(l.descriptor + 8, 21); rejected = false;
    try { preconditions(m, l); } catch (...) { rejected = true; }
    test(rejected, "unknown descriptor rejected");
    m.put<uint32_t>(l.descriptor + 8, 20);
    in = Installation{reinterpret_cast<Address>(ZoneName.c_str()), baseline[4], false};
    in.touched=true;
    cell = in.allocation; setField<Address>(data[4], 0x1b0, l.descriptor);
    std::memcpy(data[4].data() + 0xeb, ZoneName.c_str() + 1, (ZoneName.size()+1) - 1);
    rollbackUnpublished(m, l, in);
    test(data == baseline && cell == 0, "partial publication rolled back");
    // A cleared request may retain our descriptor and name-cell allocation.
    data=baseline;cell=reinterpret_cast<Address>(ZoneName.c_str());
    data[4][0x21]=3;setField<Address>(data[4],0x1b0,l.descriptor);
    const auto cleared=data;
    Installation renewed{cell};renewed.republishing=true;
    publish(m,l,renewed);
    test(renewed.committed&&cell==renewed.allocation&&nameAt(data[4],0)==ZoneName.c_str(),"owned idle request republished");
    test(data[4][0x20]==0&&data[4][0x21]==3,"native state bytes preserved on republication");
    auto changed=cleared;setField<Address>(changed[4],0x1b0,l.descriptor);setField<uint32_t>(changed[4],0x24,0);std::memcpy(changed[4].data()+0xea,ZoneName.c_str(),(ZoneName.size()+1));
    test(data==changed,"republication changes only witnessed request fields");
    data=cleared;Installation partial{cell,cleared[4],false};partial.originalNameCell=cell;partial.touched=true;partial.republishing=true;
    std::memcpy(data[4].data()+0xeb,ZoneName.c_str()+1,(ZoneName.size()+1)-1);
    rollbackUnpublished(m,l,partial);
    test(data==cleared&&cell==partial.originalNameCell,"partial republication restores owned name cell");
    data=baseline;cell=reinterpret_cast<Address>(descriptor.data());const auto foreign=data;
    Installation conflict{reinterpret_cast<Address>(ZoneName.c_str())};conflict.republishing=true;rejected=false;
    try{publish(m,l,conflict);}catch(...){rejected=true;rollbackUnpublished(m,l,conflict);}
    test(rejected&&data==foreign&&cell==reinterpret_cast<Address>(descriptor.data()),"foreign name cell is never changed");
    data=baseline;cell=0;data[4][0x20]=1;data[4][0x21]=3;
    test(!idleOwnedSlot(data[4],l.descriptor),"active unnamed request is not reclaimable");
    data=baseline;cell=0;data[4][0x21]=3;
    Installation resetCell{reinterpret_cast<Address>(ZoneName.c_str())};resetCell.republishing=true;
    publish(m,l,resetCell);
    test(resetCell.committed&&cell==resetCell.allocation&&nameAt(data[4],0)==ZoneName.c_str(),"fully engine-cleared request and name cell republished");
    data=baseline;cell=reinterpret_cast<Address>(ZoneName.c_str());data[4][0x21]=3;
    setField<Address>(data[4],0x1b0,reinterpret_cast<Address>(ZoneName.c_str()));const auto otherDescriptor=data;
    Installation refused{cell};refused.republishing=true;rejected=false;
    try{publish(m,l,refused);}catch(...){rejected=true;rollbackUnpublished(m,l,refused);}
    test(rejected&&data==otherDescriptor&&cell==refused.allocation,"empty slot with another descriptor is never changed");
    for(unsigned high:{0u,3u}){
        data=baseline;cell=reinterpret_cast<Address>(ZoneName.c_str());data[4][0x21]=static_cast<unsigned char>(high);
        std::fill(data[4].begin()+0xea,data[4].begin()+0x1aa,static_cast<unsigned char>(0x5a));
        for(size_t at:{0xeau,0x12au,0x16au})data[4][at]=0;
        test(idleOwnedSlot(data[4],l.descriptor)&&!freeSlot(data[4]),"empty C strings with stale tails are reclaimable only after ownership");
        const auto stale=data;Installation next{cell};next.republishing=true;publish(m,l,next);
        auto intended=stale;setField<Address>(intended[4],0x1b0,l.descriptor);setField<uint32_t>(intended[4],0x24,0);std::memcpy(intended[4].data()+0xea,ZoneName.c_str(),ZoneName.size()+1);
        test(data==intended&&next.committed,"stale-tail republication changes only the original native fields");
        data=stale;data[4][0x12a]='x';test(!idleOwnedSlot(data[4],l.descriptor),"a secondary live name remains busy");
        data=stale;data[4][0x16a]='x';test(!idleOwnedSlot(data[4],l.descriptor),"a tertiary live name remains busy");
        data=stale;data[4][0x20]=1;test(!idleOwnedSlot(data[4],l.descriptor),"active stale-tail slot remains busy");
    }
    wchar_t own[MAX_PATH]{}; GetModuleFileNameW(nullptr, own, MAX_PATH);
    test(sha256(own).size() == 64, "SHA256 implementation");
    std::cout << "PASS: dynamic publication, rollback, ownership and native-state guards on private allocations; game untouched.\n";
}

int main(){selfTest();ZoneName="another_package";selfTest();}
