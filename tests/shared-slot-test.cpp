#include "shared-slot.hpp"
#include "sharing-policy.hpp"

static unsigned checks = 0;
static void check(bool condition, const char* message) {
    require(condition, std::string("SELF-TEST: ") + message);
    ++checks;
}
static constexpr char Pack[] = "global_stream_cfwd_mp";
static std::atomic<bool> engineError{false}, requestActive{false};
static std::vector<std::string> lines;
static void collect(const std::string& text) noexcept {
    try { lines.push_back(text); } catch (...) {}
}

struct World {
    Table data{};
    std::array<unsigned char, 0x40> descriptor{};
    Address cell = 0, page = 0;
    Layout layout{};
    Memory memory{GetCurrentProcess()};
    std::string zone;
    explicit World(const std::string& name) : zone(name) {
        layout = {reinterpret_cast<Address>(&data), reinterpret_cast<Address>(descriptor.data()),
            reinterpret_cast<Address>(&cell)};
        memory.put<uint64_t>(layout.descriptor, DescriptorHash);
        memory.put<uint32_t>(layout.descriptor + 8, 20);
        memory.put<uint32_t>(layout.descriptor + 12, 1);
        memory.put<Address>(layout.descriptor + 16, layout.nameCell);
        descriptor[0x18] = 1;
        descriptor[0x19] = 0x45;
        setRequest(2, "frontend_ui", 2);
        setRequest(3, "global_stream_mp", 2);
        std::memcpy(data[3].data() + 0x12a, "global_shared_mp", 17);
        setRequest(8, "ingame_zm", 2);
        page = reinterpret_cast<Address>(VirtualAlloc(nullptr, 4096,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(page != 0, "fixture allocation");
        memory.write(page, zone.c_str(), zone.size() + 1);
    }
    ~World() { VirtualFree(reinterpret_cast<void*>(page), 0, MEM_RELEASE); }
    void setRequest(size_t index, const char* name, unsigned char state) {
        std::memset(data[index].data() + 0xea, 0, 0x40);
        std::memcpy(data[index].data() + 0xea, name, std::strlen(name) + 1);
        data[index][0x20] = state;
        setField<Address>(data[index], 0x1b0, layout.descriptor);
    }
};

static void arm(World& world, bool allowFrontEnd = false, bool armed = true) {
    auto& state = shared_slot::hook;
    state.layout = world.layout;
    state.page = world.page;
    state.zone = world.zone;
    state.original = &WriteProcessMemory;
    state.armed.store(armed);
    state.engineError = &engineError;
    state.requestActive = &requestActive;
    state.allowFrontEnd = allowFrontEnd;
    state.publications = state.commits = state.notes = 0;
    state.log = &collect;
    engineError.store(false);
    requestActive.store(false);
    lines.clear();
}
static bool publish(World& world, shared_slot::WriteFn writer = &shared_slot::observe) {
    const Address address = world.layout.requests + ExtraSlot * Stride;
    const Address name = reinterpret_cast<Address>(Pack), owner = world.layout.descriptor;
    const uint32_t flags = 0;
    SIZE_T written = 0;
    return writer(GetCurrentProcess(), reinterpret_cast<void*>(world.layout.nameCell), &name, 8, &written) &&
        writer(GetCurrentProcess(), reinterpret_cast<void*>(address + 0x1b0), &owner, 8, &written) &&
        writer(GetCurrentProcess(), reinterpret_cast<void*>(address + 0x24), &flags, 4, &written) &&
        writer(GetCurrentProcess(), reinterpret_cast<void*>(address + 0xeb), Pack + 1, sizeof Pack - 1, &written) &&
        writer(GetCurrentProcess(), reinterpret_cast<void*>(address + 0xea), Pack, 1, &written) && written == 1;
}
static bool clear(World& world) {
    const Address address = world.layout.requests + ExtraSlot * Stride, zero = 0;
    const std::array<char, 0x3f> empty{};
    SIZE_T written = 0;
    bool success = shared_slot::observe(GetCurrentProcess(), reinterpret_cast<void*>(address + 0x1b0),
        &zero, 8, &written) != FALSE;
    for (size_t index = 0; index < 3; ++index) {
        success = shared_slot::observe(GetCurrentProcess(), reinterpret_cast<void*>(address + 0xea + index * 0x40),
            empty.data(), 1, &written) && success;
        success = shared_slot::observe(GetCurrentProcess(), reinterpret_cast<void*>(address + 0xeb + index * 0x40),
            empty.data(), empty.size(), &written) && success;
    }
    return shared_slot::observe(GetCurrentProcess(), reinterpret_cast<void*>(world.layout.nameCell),
        &zero, 8, &written) && success;
}
static Table unshared(Table baseline, Address descriptor) {
    setField<Address>(baseline[4], 0x1b0, descriptor);
    setField<uint32_t>(baseline[4], 0x24, 0);
    std::memcpy(baseline[4].data() + 0xea, Pack, sizeof Pack);
    return baseline;
}

static void run(const std::string& zone) {
    using namespace shared_slot;
    {
        World world(zone);
        arm(world);
        const auto baseline = world.data;
        const auto descriptor = world.descriptor;
        check(publish(world), "natural publication succeeds");
        check(nameAt(world.data[4], 0) == Pack && nameAt(world.data[4], 1) == zone, "two requested names");
        check(field<Address>(world.data[4], 0x1b0) == privateDescriptor(world.page), "private owner");
        check(world.cell == reinterpret_cast<Address>(Pack) && world.descriptor == descriptor, "native cell and descriptor unchanged");
        check(world.memory.get<uint32_t>(privateDescriptor(world.page) + 12) == 2 &&
            world.memory.get<Address>(privateDescriptor(world.page) + 16) == world.page + NamesOffset &&
            world.memory.get<uint16_t>(privateDescriptor(world.page) + 0x18) == 0x4501, "descriptor metadata preserved");
        auto expected = unshared(baseline, privateDescriptor(world.page));
        std::memcpy(expected[4].data() + 0x12a, zone.c_str(), zone.size() + 1);
        check(world.data == expected && hook.publications == 1, "only intended request fields changed");
        world.data[4][0x20] = 2;
        std::memcpy(world.data[4].data() + 0x2a, Pack, sizeof Pack);
        check(!loaded(world.data[4], world.page, zone), "stale state 2 without second working name is not success");
        std::memcpy(world.data[4].data() + 0x6a, zone.c_str(), zone.size() + 1);
        check(loaded(world.data[4], world.page, zone), "both working names must be acknowledged");
        const auto loadedState = world.data;
        Leftover state;
        for (uint64_t tick : {1000ull, 3000ull, 20000ull})
            check(tidy(world.memory, world.layout, world.page, zone, state, tick).empty() &&
                world.data == loadedState, "loaded shared request is never cleared");
        world.data[4][0x20] = 0;
        check(clear(world), "Kronos clear forwarded");
        std::memset(world.data[4].data() + 0x2a, 0, 0xc0);
        check(world.data == baseline && world.cell == 0, "natural clear leaves original empty request");
        check(publish(world) && hook.publications == 2, "natural republication shares again");
    }
    {
        World world(zone);
        world.setRequest(8, "ingame_mp", 2);
        arm(world);
        const auto baseline = world.data;
        check(publish(world) && world.data == unshared(baseline, world.layout.descriptor),
            "front-end publication remains original without opt-in");
        check(hook.publications == 0, "default preloading disabled");
        check(clear(world), "clear before preload test");
        arm(world, true);
        check(publish(world) && nameAt(world.data[4], 1) == zone && hook.publications == 1,
            "opted-in static fixture joins initial front-end publication");
    }
    for (size_t incomplete : {size_t{2}, size_t{3}}) {
        World world(zone);
        world.setRequest(8, "ingame_mp", 2);
        world.data[incomplete][0x20] = 1;
        arm(world, true);
        const auto baseline = world.data;
        check(publish(world) && world.data == unshared(baseline, world.layout.descriptor),
            "preload requires loaded front end and global packs");
    }
    for (const auto& nativeState : std::array<std::array<unsigned char, 2>, 5>{{
        {{1, 0}}, {{1, 5}}, {{2, 0}}, {{3, 0}}, {{3, 1}}
    }}) {
        World world(zone);
        world.data[4][0x20] = nativeState[0];
        world.data[4][0x21] = nativeState[1];
        arm(world, true);
        const auto baseline = world.data;
        check(publish(world) && world.data == unshared(baseline, world.layout.descriptor) &&
            hook.publications == 0, "busy native state left byte-for-byte as Kronos published");
    }
    {
        World world(zone);
        world.data[4][0x21] = 3;
        arm(world);
        check(publish(world) && nameAt(world.data[4], 1) == zone && world.data[4][0x21] == 3,
            "naturally idle 0.3 publication accepted without altering native state");
    }
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        World world(zone);
        arm(world, true, scenario != 0);
        if (scenario == 1) engineError.store(true);
        if (scenario == 2) hook.publications = MaximumPublications;
        if (scenario == 3) std::memcpy(world.data[4].data() + 0x16a, "other", 6);
        if (scenario == 4) world.memory.put<uint32_t>(world.layout.descriptor + 8, 21);
        const auto baseline = world.data;
        check(publish(world) && world.data == unshared(baseline, world.layout.descriptor),
            "unarmed, error, limit, occupied name and unknown profile preserve original publication");
    }
    {
        World world(zone);
        arm(world, true);
        world.cell = reinterpret_cast<Address>(Pack);
        const Address foreign = world.page;
        setField<Address>(world.data[4], 0x1b0, foreign);
        std::memcpy(world.data[4].data() + 0xeb, Pack + 1, sizeof Pack - 1);
        auto expected = world.data;
        expected[4][0xea] = Pack[0];
        SIZE_T written = 0;
        check(observe(GetCurrentProcess(), reinterpret_cast<void*>(world.layout.requests + 4 * Stride + 0xea),
            Pack, 1, &written) && world.data == expected, "foreign owner never replaced");
        uint32_t value = 17, destination = 0;
        check(observe(GetCurrentProcess(), &destination, &value, sizeof value, &written) &&
            destination == value && written == sizeof value, "unrelated write forwarded");
    }
    {
        World world(zone);
        arm(world);
        check(publish(world), "publication before native cleanup");
        world.data[4][0x20] = 0;
        world.data[4][0x21] = 3;
        world.data[4][0xea] = 0;
        setField<Address>(world.data[4], 0x1b0, Address{0});
        Leftover state;
        check(tidy(world.memory, world.layout, world.page, zone, state, 1000).empty(), "leftover only observed initially");
        world.data[4][0x24] = 1;
        check(tidy(world.memory, world.layout, world.page, zone, state, 2100).empty() &&
            nameAt(world.data[4], 1) == zone && state.since == 2100, "changed request restarts settling window");
        check(tidy(world.memory, world.layout, world.page, zone, state, 3099).empty(), "cleanup waits entire settling window");
        check(!tidy(world.memory, world.layout, world.page, zone, state, 3100).empty() &&
            nameAt(world.data[4], 1).empty() && world.data[4][0xeb] == 'l' &&
            world.data[4][0x21] == 3, "only settled owned secondary name cleaned");
    }
}

int main() {
    try {
        using Json = nlohmann::json;
        check(!sharing_policy::preloadAllowed(Json::object()), "manifest defaults to no early load");
        check(sharing_policy::preloadAllowed(Json{{"carryForwardPreload", true}}), "explicit preload enabled");
        check(!sharing_policy::preloadAllowed(Json{{"carryForwardPreload", false}}), "explicit preload disabled");
        for (const auto& invalid : {Json("true"), Json(1), Json(nullptr), Json::array()}) {
            bool rejected = false;
            try { sharing_policy::preloadAllowed(Json{{"carryForwardPreload", invalid}}); }
            catch (...) { rejected = true; }
            check(rejected, "invalid preload policy rejected");
        }
        run("rdf_geometry_probe_v01");
        run("another_package");
        std::cout << "PASS: " << checks << " passive sharing, busy-state, preload, cleanup and acknowledgement checks; private allocations only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
