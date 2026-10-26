// D-129: each generation's original state is its own. A thread entering one generation never waits for
// another generation's load or execution; one generation is entered by one thread at a time.
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
struct ConcurrencyGate {
    std::mutex mx;std::condition_variable cv;bool entered=false,released=false;
    void hold(){std::unique_lock<std::mutex> lock(mx);entered=true;cv.notify_all();cv.wait_for(lock,std::chrono::seconds(10),[&]{return released;});}
    bool wait_entered(){std::unique_lock<std::mutex> lock(mx);return cv.wait_for(lock,std::chrono::seconds(10),[&]{return entered;});}
    void release(){{std::lock_guard<std::mutex> lock(mx);released=true;}cv.notify_all();}
};
// Holds a load inside its generation's scope (the extension's declarations are read there).
struct LoadGateExtension final : efmi::frontend::RuntimeExtension {
    ConcurrencyGate gate;
    std::wstring declarations() override {gate.hold();return {};}
    std::vector<std::wstring> install(efmi::frontend::DrawPlanAdapter&) override {return {};}
};
// Holds one matched draw inside its generation's scope.
struct DrawGateExtension final : efmi::frontend::RuntimeExtension {
    ConcurrencyGate gate;std::atomic<bool> armed{false};
    std::vector<std::wstring> install(efmi::frontend::DrawPlanAdapter&) override {return {};}
    bool before_draw(const efmi::frontend::RuntimeDraw&,CommandListState&,std::wstring&) override {if(armed.exchange(false))gate.hold();return true;}
};
static void runtime_concurrency_cases(const std::filesystem::path& folder){
    using namespace efmi::frontend;const auto root=std::filesystem::absolute(folder/"concurrency");std::filesystem::create_directories(root);
    auto generation=[&](const char* name,unsigned hash,unsigned count,std::shared_ptr<RuntimeExtension> extension){
        const auto path=root/(std::string(name)+".ini");
        char text[512];
        snprintf(text,sizeof(text),"namespace = %s\n[Constants]\nglobal $presents = 0\nglobal $draws = 0\n[Present]\n$presents = $presents + 1\n"
                 "[TextureOverrideMesh]\nhash = %08x\nmatch_index_count = 30\n$draws = $draws + 1\nhandling = skip\ndrawindexed = %u,0,0\n",name,hash,count);
        write(path,text);
        RuntimeConfig config{root,{path},root/"settings",name};std::string why;
        auto runtime=IniRuntime::load(config,std::move(extension),why);
        if(!runtime)throw std::runtime_error("concurrency fixture "+std::string(name)+": "+why);
        return runtime;
    };
    auto run=[](IniRuntime& runtime,unsigned hash,unsigned expected){
        std::string why;RuntimeInput input;input.foreground=true;efmi::assets::FilePlan plan;
        RuntimeDraw draw;draw.hash=hash;draw.count=30;draw.frame=1;
        return runtime.present(input,1,0.1f,why) && runtime.draw(draw,plan,why) && !plan.items.empty() && plan.items.back().index_count==expected;
    };
    auto value=[](IniRuntime& runtime,const char* name,const char* key){return runtime.status().variables.at(std::string("$\\")+name+"\\"+key);};
    auto live=generation("conc_live",0x12345678,3,nullptr);
    {
        // A load holding its generation's scope: the live generation presents and draws meanwhile.
        auto gated=std::make_shared<LoadGateExtension>();
        auto loading=std::async(std::launch::async,[&]{return generation("conc_loading",0x22345678,6,gated);});
        const bool entered=gated->gate.wait_entered();
        auto running=std::async(std::launch::async,[&]{return run(*live,0x12345678,3);});
        const bool free=running.wait_for(std::chrono::seconds(2))==std::future_status::ready;
        gated->gate.release();
        const bool ran=running.get();auto loaded=loading.get();
        check(entered,"the gated load reached its generation's scope");
        check(free && ran,"a running generation presents and draws while another generation's load holds its scope");
        check(loaded && run(*loaded,0x22345678,6),"the held load completes and runs after its release");
    }
    {
        // One generation's draw held: another generation presents and draws meanwhile; the held one's own
        // Present waits for its draw, as one generation is one thread at a time.
        auto held=std::make_shared<DrawGateExtension>();
        auto first=generation("conc_held",0x32345678,9,held);
        held->armed=true;
        auto drawing=std::async(std::launch::async,[&]{return run(*first,0x32345678,9);});
        const bool entered=held->gate.wait_entered();
        auto other=std::async(std::launch::async,[&]{return run(*live,0x12345678,3);});
        const bool free=other.wait_for(std::chrono::seconds(2))==std::future_status::ready;
        auto same=std::async(std::launch::async,[&]{std::string why;RuntimeInput input;return first->present(input,2,0.2f,why);});
        const bool waited=same.wait_for(std::chrono::milliseconds(300))==std::future_status::timeout;
        held->gate.release();
        const bool drew=drawing.get(),ran=other.get(),presented=same.get();
        check(entered,"the gated draw reached its generation's scope");
        check(free && ran,"a generation presents and draws while another generation's draw holds its scope");
        check(waited && drew && presented,"a generation's Present waits for its own draw in progress");
    }
    {
        // Two generations driven from two threads while a third loads and retires generations: each
        // generation's counters and plans stay its own.
        auto other=generation("conc_other",0x42345678,12,nullptr);
        const double live_before=value(*live,"conc_live","presents"),other_before=value(*other,"conc_other","presents");
        std::atomic<bool> wrong{false};constexpr unsigned rounds=1500;
        auto a=std::async(std::launch::async,[&]{for(unsigned i=0;i<rounds;++i)if(!run(*live,0x12345678,3))wrong=true;});
        auto b=std::async(std::launch::async,[&]{for(unsigned i=0;i<rounds;++i)if(!run(*other,0x42345678,12))wrong=true;});
        auto c=std::async(std::launch::async,[&]{for(unsigned i=0;i<12;++i){auto g=generation("conc_churn",0x52345678,15,nullptr);if(!run(*g,0x52345678,15))wrong=true;}});
        a.get();b.get();c.get();
        check(!wrong,"concurrent generations each plan their own draws");
        check(value(*live,"conc_live","presents")==live_before+rounds && value(*other,"conc_other","presents")==other_before+rounds,
              "concurrent generations each count their own presentations");
    }
}
