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
}
