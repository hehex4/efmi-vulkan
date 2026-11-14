#include "../layer/src/character_backend_internal.h"

namespace hunting_idle_fixture {
template <typename Tag, typename Tag::type Member> struct Expose {
    friend typename Tag::type member(Tag) { return Member; }
};
struct Clock { using type=uint64_t efvk::Hunting::*;friend type member(Clock); };
template struct Expose<Clock,&efvk::Hunting::last_key_us_>;
static std::string overlay;
static void show(Device*,const char* text){overlay=text?text:"";}
static unsigned publications=0;
static void analysis(Device*,const char*,uint32_t){++publications;}
}
static void hunting_idle_cases()
{
    Hunting hunt;efmi::frontend::HuntingSettings settings;settings.keys=true;settings.hunting=1;hunt.configure(settings);
    DrawFacts facts;facts.modules.mask=1;facts.modules.hashes[0]=0x12345678;
    auto empty=[&](){JsonWriter w;hunt.status(w);return w.out.find("\"visited\":{\"vertex\":0")!=std::string::npos;};
    hunt.visit(facts);hunt.frame({});
    check(empty(),"hunting clears visited sets each idle frame before the first input event");
    hunt.*member(hunting_idle_fixture::Clock())=host_time_us()-61000000ull;
    hunt.visit(facts);hunt.frame({});
    check(empty(),"hunting expires visited sets after sixty seconds without a registered event");
    hunt.visit(facts);hunt.frame({});
    check(empty(),"hunting keeps expiring every idle frame until the next registered event");
    hunt.act(efmi::frontend::HuntingAction::InputActivity,nullptr,{});
    hunt.visit(facts);hunt.frame({});
    check(!empty(),"a registered non-hunting input event stabilizes the visited collection");
    Device device;device.hunting_overlay=hunting_idle_fixture::show;
    hunt.frame({},&device);
    check(hunting_idle_fixture::overlay=="VS:0/0","persistent hunting line preserves the original unselected count format");
    hunt.act(efmi::frontend::HuntingAction::NextVertex,nullptr,{});hunt.frame({},&device);
    check(hunting_idle_fixture::overlay=="VS:1/1","persistent hunting line follows the current selected shader");
    hunt.act(efmi::frontend::HuntingAction::Toggle,nullptr,{});hunt.frame({},&device);
    check(hunting_idle_fixture::overlay.empty(),"disabling hunting removes its host-owned persistent line");
    CharacterBackend staged;staged.device=&device;device.analysis_frame=hunting_idle_fixture::analysis;
    hunting_idle_fixture::publications=0;hunting_idle_fixture::overlay="active backend";
    shader_hooks_frame(staged,false);
    check(!hunting_idle_fixture::publications && hunting_idle_fixture::overlay=="active backend",
        "staged backend preparation cannot publish analysis or replace the active hunting display");
    shader_hooks_frame(staged);
    check(hunting_idle_fixture::publications==1 && hunting_idle_fixture::overlay.empty(),
        "active presentation publishes its own analysis and hunting state");
}
