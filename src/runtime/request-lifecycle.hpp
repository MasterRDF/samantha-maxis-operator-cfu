#pragma once
#include <cstdint>

// Debounce an engine-cleared slot. The caller separately validates the pinned
// game descriptor and the actual request fields immediately before publishing.
namespace request_lifecycle {
enum class Decision { wait, republish, foreignOwner, limitReached };
struct Policy {
    static constexpr unsigned MaximumPublications=8;
    static constexpr uint64_t SettleMs=2000;
    unsigned publications=1;
    bool tracking=false;
    uint64_t since=0,fingerprint=0;
    Decision poll(bool zombiesReady,bool idleSlot,uintptr_t nameCell,uintptr_t ownPage,
                  uint64_t now,uint64_t observedFingerprint) {
        if(nameCell!=0&&nameCell!=ownPage){tracking=false;return Decision::foreignOwner;}
        if(!zombiesReady||!idleSlot){tracking=false;return Decision::wait;}
        if(!tracking||fingerprint!=observedFingerprint||now<since){
            tracking=true;since=now;fingerprint=observedFingerprint;return Decision::wait;
        }
        if(now-since<SettleMs)return Decision::wait;
        return publications<MaximumPublications?Decision::republish:Decision::limitReached;
    }
    void published(){++publications;tracking=false;}
    void retryLater(){tracking=false;}
};
}
