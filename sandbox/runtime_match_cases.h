// Source CheckTextureOverride order and independent root phase state.
#include "../frontend/xxmi/runtime_host.h"
struct MatchReferenceExtension final : efmi::frontend::RuntimeExtension {
    unsigned completed=0,failed=0;
    std::vector<std::pair<bool,float>> queries;
    std::vector<std::wstring> install(efmi::frontend::DrawPlanAdapter& adapter) override {
        adapter.texture_queries([this](unsigned slot,const auto&,auto& state,auto& identity,std::wstring&){
            if(slot==1){queries.push_back({state.post,state.rt_width});state.rt_width=101;identity.bound=false;}
            else {identity.bound=true;identity.hash=0x12345678;}return true;});return {};
    }
    bool before_draw(const efmi::frontend::RuntimeDraw&,CommandListState& state,std::wstring&) override {state.rt_width=73;return true;}
    void after_draw() noexcept override {++completed;}
    void execution_failed() noexcept override {++failed;}
};
static void runtime_match_cases(const std::filesystem::path& folder){
    using namespace efmi::frontend;const auto root=std::filesystem::absolute(folder/"match-phases");std::filesystem::create_directories(root);
    for(unsigned mode=0;mode<8;++mode){const auto path=root/(std::to_string(mode)+".ini");
        std::string text="namespace = match_phase\n[Constants]\nglobal $trace = 0\nglobal $peek = 0\nglobal $stop = "+std::to_string(mode)+R"ini(
[TextureOverrideA]
hash = 12345678
match_priority = )ini"+(mode==4?"2":"0")+R"ini(
$trace = $trace * 10 + 1
if $stop == 1 || $stop == 7
pre handling = abort
endif
if $stop == 6
handling = abort
endif
if $stop == 5
$peek = ps-t1
endif
post $trace = $trace * 10 + 3
if $stop == 2
post handling = abort
endif
if $stop == 5
post $peek = ps-t1
endif
[TextureOverrideB]
hash = 12345678
match_priority = 1
)ini"+(mode==3?"match_index_count = 99\n":"")+R"ini(
$trace = $trace * 10 + 2
if $stop == 5
$peek = ps-t1
endif
post $trace = $trace * 10 + 4
if $stop == 5
post $peek = ps-t1
endif
[Present]
$trace = 0
)ini"+(mode==7?"pre checktextureoverride = ps-t0\npost $trace = $trace * 10 + 9\n":"checktextureoverride = ps-t0\n");
        write(path,text.c_str());RuntimeConfig config{root,{path},root/"settings","phase"+std::to_string(mode)};std::string why;
        auto extension=std::make_shared<MatchReferenceExtension>();auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);
        RuntimeDraw draw;draw.hash=0x12345678;draw.count=30;efmi::assets::FilePlan output;output.skip_original=true;
        check(runtime->draw(draw,output,why),"Multiple source matches execute one bounded draw transaction");
        const float expected[]={1234,134,123,13,2143,1234,1,134};auto trace=[&](){return runtime->status().variables.at("$\\match_phase\\trace");};
        if(trace()!=expected[mode])printf("Matched phase mode%u: actual %.0f expected %.0f\n",mode,trace(),expected[mode]);
        check(trace()==expected[mode],"Match priority, draw filtering and shared phase abort preserve source order");
        check(!output.skip_original && output.items.empty() && extension->completed==1 && !extension->failed,"Matched draw publishes once and completes once");
        if(mode==5){const std::vector<std::pair<bool,float>> wanted={{false,73},{false,101},{true,-1},{true,101}};
            check(extension->queries==wanted,"Queries share state within matches and reset only at the independent post root");}
        if(mode!=3){check(runtime->present({},1,0,why),"Equivalent original pre/post texture checking executes");
            check(trace()==(mode==7?19:expected[mode]),"Source nested combined phases keep abort while independent roots reset it");}
        const auto before=trace();draw.hash=0;check(runtime->draw(draw,output,why) && extension->completed==2 && trace()==before,"Unmatched draw still completes without executing source lists");
    }
    const auto path=root/"budget.ini";std::string text="namespace = match_budget\n";
    for(unsigned section=0;section<2;++section){text+="[TextureOverride"+std::to_string(section)+"]\nhash = 12345678\nmatch_priority = "+std::to_string(section)+"\n";
        for(unsigned draw=0;draw<130;++draw)text+="drawindexed = 3, 0, 0\n";}
    write(path,text.c_str());RuntimeConfig config{root,{path},root/"settings","budget"};std::string why;auto extension=std::make_shared<MatchReferenceExtension>();
    auto runtime=IniRuntime::load(config,extension,why);if(!runtime)throw std::runtime_error(why);RuntimeDraw draw;draw.hash=0x12345678;draw.count=30;
    efmi::assets::FilePlan output;output.skip_original=true;output.items.resize(1);output.items[0].index_count=777;
    check(!runtime->draw(draw,output,why) && why.find("budget")!=std::string::npos,"Matching another section cannot reset the draw budget");
    check(output.skip_original && output.items.size()==1 && output.items[0].index_count==777 && !runtime->status().draws,
        "Rejected multi-match draw preserves the caller plan and success counter");
    check(extension->completed==1 && extension->failed==1,"Failed multi-match transaction notifies failure and completion once");
}
