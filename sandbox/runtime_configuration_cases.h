// Original LoadConfigFile/ParseIncludedIniFiles behavior without Mod discovery.
static void runtime_configuration_cases(const std::filesystem::path& fixtures)
{
    const auto root = std::filesystem::absolute(fixtures / "configuration");
    std::filesystem::create_directories(root / "Mods" / "A");
    std::filesystem::create_directories(root / "Mods" / "B");
    std::filesystem::create_directories(root / "Mods" / "DISABLED-unused");
    std::filesystem::create_directories(root / "Libraries");
    write(root / "d3dx.ini", R"ini([Constants]
global $root_value = 17
[Include]
include = Libraries/helper.ini
include_recursive = Mods
exclude_recursive = DISABLED*
exclude_recursive = desktop.ini
)ini");
    write(root / "Libraries" / "helper.ini", R"ini(namespace = helper
[Constants]
global $calls = 0
[CommandListTouch]
$calls = $calls + 1
)ini");
    write(root / "Mods" / "A" / "first.ini", R"ini(namespace = shared
[Constants]
global $alpha = 0
global $initializer_calls = 0
[ResourceNeverUsed]
type = Buffer
stride = 12
filename = deliberately-missing.buf
[CommandListInitializeMergedSkeleton]
$initializer_calls = $initializer_calls + 1
[TextureOverrideFirst]
hash = 12345678
match_index_count = 30
$alpha = 7
run = CommandList\helper\Touch
run = CommandListSecond
handling = skip
drawindexed = 3,0,0
)ini");
    write(root / "Mods" / "B" / "second.ini", R"ini(namespace = shared
[Constants]
global $beta = 0
[CommandListSecond]
$beta = $beta + 5
)ini");
    write(root / "Mods" / "DISABLED-unused" / "hidden.ini", R"ini(namespace = hidden
[Constants]
global $unexpected = 1
)ini");
    RuntimeConfig config{root, {}, root / "settings", "configuration"};
    config.installation = root;
    std::string reason;
    auto runtime = IniRuntime::load(config, {}, reason);
    if (!runtime) throw std::runtime_error("Original configuration load: " + reason);
    check(runtime->status().variables.at("$root_value") == 17, "Root configuration constants execute");
    check(runtime->status().variables.at("$\\shared\\initializer_calls") == 0,
          "A named initializer is not executed by loader discovery");
    check(runtime->status().variables.count("$\\hidden\\unexpected") == 0,
          "Original recursive exclusions apply to the complete configuration");
    check(!std::filesystem::exists(root / "Mods" / "A" / "deliberately-missing.buf"),
          "Unused missing resources do not gate configuration loading");
    RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30;
    efmi::assets::FilePlan plan;
    check(runtime->draw(draw, plan, reason), "Global hash rules execute without a package");
    check(plan.skip_original && plan.items.size() == 1 && plan.items[0].index_count == 3,
          "Original command lists supply the draw plan");
    const auto state = runtime->status();
    check(state.variables.at("$\\shared\\alpha") == 7 && state.variables.at("$\\shared\\beta") == 5,
          "Two included files sharing a namespace both contribute their distinct sections");
    check(state.variables.at("$\\helper\\calls") == 1,
          "Cross-directory command references execute in the same registry");
    auto second = IniRuntime::load(config, {}, reason);
    check(bool(second), "A complete successor configuration loads independently");
    check(second->status().variables.at("$\\shared\\beta") == 0 && runtime->status().variables.at("$\\shared\\beta") == 5,
          "Old recorded generation state survives construction of its successor");
    // Shader queries use the same included registry as native draw interception.
    // No character extension or source-API register declarations are involved.
    std::string shaders="[Constants]\nglobal $tail = 0\n";
    const char* names[]={"vs","hs","ds","gs","ps","cs"};
    for(unsigned i=0;i<6;++i)shaders+="global $"+std::string(names[i])+" = 99\n";
    shaders+="[TextureOverrideQuery]\nhash = 11223344\n";
    for(const auto* name:names)shaders+="$"+std::string(name)+" = "+name+"\n";
    shaders+="$tail = $tail + 1\n";
    for(unsigned i=0;i<6;++i){
        shaders+="[ShaderOverride"+std::to_string(i)+"]\nhash = "+std::to_string(i+1)+"\n";
        if(i!=4)shaders+="filter_index = "+std::to_string(i+21)+"\n";
    }
    write(root/"d3dx.ini",shaders.c_str());
    runtime=IniRuntime::load(config,{},reason);
    if(!runtime)throw std::runtime_error("Native shader query load: "+reason);
    draw={};draw.hash=0x11223344;draw.native_calls=true;draw.shader_stages=63;draw.stage_hash_mask=63;
    for(unsigned i=0;i<6;++i)draw.stage_hashes[i]=i+1;
    check(runtime->draw(draw,plan,reason),"All six native shader filter queries execute in the original registry");
    auto values=runtime->status().variables;
    for(unsigned i=0;i<6;++i)check(values.at("$"+std::string(names[i]))==(i==4?1.0f:float(i+21)),
        "A matching native stage returns its explicit filter or the original default one");
    draw.shader_stages=33;draw.stage_hash_mask=1;draw.stage_hashes[0]=9;
    check(runtime->draw(draw,plan,reason),"Unmatched and unknown-hash native shaders still execute queries");
    values=runtime->status().variables;
    for(unsigned i=0;i<6;++i)check(values.at("$"+std::string(names[i]))==0 &&
        std::signbit(values.at("$"+std::string(names[i])))==(i!=0 && i!=5),
        "Native filter queries distinguish bound positive zero from unbound negative zero");
    shaders+="[ShaderOverrideComputeSkip]\nhash = 7\nfilter_index = 29\nif cs == 29\nhandling = skip\nendif\n";
    write(root/"d3dx.ini",shaders.c_str());runtime=IniRuntime::load(config,{},reason);
    if(!runtime)throw std::runtime_error(reason);
    draw={};draw.native_calls=true;draw.compute=true;draw.shader_stages=32;draw.stage_hash_mask=32;draw.stage_hashes[5]=7;
    check(runtime->shader_draw(draw,plan,reason) && plan.skip_original,
        "Native compute overrides query their current filter before skipping the original call");
    shaders+="[ShaderOverrideUnsafeDeclaration]\nhash = 8\nif cs->srv_stride(0) == 4\nhandling = skip\nendif\n";
    write(root/"d3dx.ini",shaders.c_str());runtime=IniRuntime::load(config,{},reason);
    if(!runtime)throw std::runtime_error(reason);
    draw.stage_hashes[5]=8;
    check(runtime->shader_draw(draw,plan,reason) && !plan.skip_original,
        "Native source-register declarations never fabricate a matching stride");
    bool warned=false;
    for(const auto& message:runtime->status().diagnostic_messages)warned|=message.find("verified source facts")!=std::string::npos;
    check(warned,"Unavailable native source-register declarations retain the original adapter warning");
    unsigned queries=0;
    draw.source_declaration=[&](wchar_t stage,unsigned slot,ShaderDeclarationQuery query,float& value,std::string& why){
        ++queries;if(stage!=L'c' || slot!=0 || query!=ShaderDeclarationQuery::ResourceStride){why="Unexpected query";return false;}
        value=4;return true;
    };
    const bool declaration_ran=runtime->shader_draw(draw,plan,reason);
    if(!declaration_ran || !plan.skip_original || queries!=2)
        std::cerr<<"Declaration query diagnostic: ran="<<declaration_ran<<" skip="<<plan.skip_original<<" queries="<<queries<<" reason="<<reason<<"\n";
    check(declaration_ran && plan.skip_original && queries==2,
        "Verified source declaration facts reach original conditional command execution");
    draw.stage_hashes[5]=9;draw.stage_hash_mask=0;queries=0;
    // Name this override through its index trigger because the current source
    // hash is intentionally unknown. Original evaluation returns before args.
    shaders+="[TextureOverrideEarlyDeclaration]\nhash = aabbccdd\n"
        "if vs->srv_stride(-1) == 0 && cs->cb_size(-1) == 0\nhandling = skip\nendif\n";
    write(root/"d3dx.ini",shaders.c_str());runtime=IniRuntime::load(config,{},reason);
    if(!runtime)throw std::runtime_error(reason);
    draw.hash=0xaabbccdd;
    check(runtime->draw(draw,plan,reason) && plan.skip_original && queries==0,
        "Unbound and unknown-hash source shaders return zero before evaluating declaration arguments");

    // The original Core enters TextureOverrides through a conditional regex
    // command, not by eagerly inserting all matching IB lists.
    for(unsigned enabled=0;enabled<2;++enabled){
        const std::string regex=std::string("[Constants]\nglobal $enabled = ")+std::to_string(enabled)+R"ini(
global $pre = 0
global $post = 0
global $hit = 0
global $v = 0
global $p = 0
[ShaderOverrideVertex]
hash = 1
filter_index = 42
$pre = $pre * 10 + 1
post $post = $post * 10 + 4
[ShaderRegexA]
shader_model = vs_5_0
filter_index = 7
$pre = $pre * 10 + 2
post $post = $post * 10 + 5
if $enabled
    checktextureoverride = ib
endif
[ShaderRegexB]
shader_model = ps_5_0
filter_index = 11
$pre = $pre * 10 + 3
post $post = $post * 10 + 6
$v = vs
$p = ps
[TextureOverrideConditional]
hash = aabbccdd
$hit = $hit + 1
post $hit = $hit + 100
handling = skip
)ini";
        write(root/"d3dx.ini",regex.c_str());runtime=IniRuntime::load(config,{},reason);
        if(!runtime)throw std::runtime_error(reason);
        draw={};draw.native_calls=true;draw.hash=0xaabbccdd;draw.shader_stages=17;draw.stage_hash_mask=17;
        draw.stage_hashes[0]=1;draw.stage_hashes[4]=2;draw.source_models[0]="vs_5_0";draw.source_models[4]="ps_5_0";
        draw.count=3;
        draw.native_resource=[](const NativeBinding& address,NativeResource& resource,std::string&){
            resource={};if(address.kind!=NativeBinding::Index)return true;
            resource.bound=true;resource.dimension=1;resource.format_known=true;resource.format=42;
            resource.identity=1;resource.buffer=1;resource.size=64;resource.range=64;resource.index_size=4;
            resource.descriptor_type=0x7fffffffu;return true;
        };
        check(runtime->draw(draw,plan,reason),"Proven models enter original patternless regex commands");
        values=runtime->status().variables;
        if(values.at("$hit")!=enabled*101 || plan.skip_original!=bool(enabled)){
            std::cerr<<"Regex conditional diagnostic: enabled="<<enabled<<" hit="<<values.at("$hit")<<" skip="<<plan.skip_original<<" reason="<<reason<<"\n";
            for(const auto& message:runtime->status().diagnostic_messages)std::cerr<<message<<"\n";
        }
        check(values.at("$hit")==enabled*101 && plan.skip_original==bool(enabled),
            "Core-style regex condition controls original IB pre/post commands without eager matches");
        check(values.at("$pre")==123 && values.at("$post")==456 && values.at("$v")==42 && values.at("$p")==11,
            "Native explicit entries precede stage-ordered regex links and keep original filter precedence");
        draw.source_models[0].clear();
        check(!runtime->draw(draw,plan,reason) && reason.find("verified source shader model")!=std::string::npos &&
            runtime->status().variables==values,"Missing original model refuses before command side effects");
        draw.source_models[0]="vs_5_0";
        check(runtime->draw(draw,plan,reason) && runtime->status().variables.at("$hit")==enabled*202,
            "Repeated model entry does not accumulate command links");
    }
}
