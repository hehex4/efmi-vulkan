static void original_core_regex(Device& d,const std::filesystem::path& root){
    const auto programs=ini_graphics_programs(root);
    std::ifstream file(root.parent_path()/"core-text/source/main.ini",std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(file)),{});
    const auto first=original.find("[ShaderRegexEnableTextureOverrides]");
    check(first!=std::string::npos,"Original Core shader regex section is present");
    const auto next=original.find("\n[",first);check(next!=std::string::npos,"Original Core shader regex section has a bounded end");
    const auto core=original.substr(first,next-first);
    // Real owned source index bytes supply a region identity. Shader hashes are
    // explicit fixture identities, not a claim about application correspondence.
    const uint32_t indices[]={0,1,2};const auto hash=efvk::crc32c(0,indices,sizeof(indices));char hex[16];snprintf(hex,sizeof(hex),"%08x",hash);
    const std::string text=std::string(ini_graphics_resources)+R"ini(
[Rendering]
track_region_hashes = 1
[Constants]
global $enable_mods = 1
global $hits = 0
x = 1
ib = ref ResourceAutoIndices
[KeyToggle]
key = no_modifiers VK_F6
type = cycle
$enable_mods = 1, 0
[Present]
clear = o0 0 0 0 0
x = 1
[CustomShaderReplacement]
vs = graphics.hlsl
ps = graphics.hlsl
topology = triangle_list
cull = none
depth_enable = false
draw = 3, 0
)ini"+core+"\n[TextureOverrideCharacter]\nhash = "+hex+R"ini(
match_index_count = 3
handling = skip
$hits = $hits + 1
x = 7
run = CustomShaderReplacement
post x = 1
)ini";
    ShaderFixture f(d,root,text.c_str(),true);f.backend->set_programs({programs.first,programs.second});
    auto state=frontend::source_graphics_defaults();state.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;state.raster.CullMode=D3D11_CULL_NONE;state.depth.DepthEnable=false;
    f.backend->set_graphics_state(state);
    auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
    auto ib=f.backend->resources().binding(L'\0',ResourceCopyTargetType::INDEX_BUFFER,0);
    frontend::SourceResourceIdentity identity;identity.tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
    identity.hash_known=true;identity.hash=hash;unsigned regions=0;
    identity.region_hash=[&](UINT offset,UINT size,uint32_t& value,std::wstring& why){
        if(offset!=0 || size!=sizeof(indices)){why=L"Unexpected fixture index region";return false;}
        ++regions;value=hash;return true;};
    f.backend->resources().identify_resource(ib,identity);
    frontend::RuntimeShaderCall call;call.call.type=DrawCall::DrawIndexed;call.call.IndexCount=3;call.call.InstanceCount=1;
    call.shaders=owned_shader_facts({programs.first,programs.second});
    call.shaders.stages[0].model=frontend::source_shader_model(programs.first.dxbc.data(),programs.first.dxbc.size());
    call.shaders.stages[4].model=frontend::source_shader_model(programs.second.dxbc.data(),programs.second.dxbc.size());
    unsigned originals=0;call.original=[&](CommandListState& current,std::wstring& why){++originals;return f.backend->execute_original(current,why);};
    frontend::RuntimeInput input;input.foreground=true;
    auto present=[&]{f.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        if(!f.ini->present(input,++f.frame,0,f.why))throw std::runtime_error(f.why);f.finish();};
    auto draw=[&](float parameter){f.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        if(!f.ini->intercept(call,f.why))throw std::runtime_error(f.why);f.finish();
        const auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
        GraphicsReferenceOptions options;if(parameter==1 && call.call.type==DrawCall::DrawIndexed)options.indices={0,1,2};
        const auto expected=native_linked_graphics(programs.first,programs.second,options,[&](auto* device,auto* context){native_ini_parameters(device,context,parameter);});
        check(actual==expected,"Unchanged original Core regex drives owned replacement and original drawing with native pixel agreement");};
    // The unprefixed source check appears in both shader phases. Each phase
    // matches the IB independently; only the texture pre list draws and counts.
    present();draw(7);check(f.variable("hits")==1 && originals==0 && regions==2,"Core indexed match suppresses the original and draws the replacement once");
    present();draw(7);check(f.variable("hits")==2 && originals==0 && regions==4,"Repeated source binding reuses one native regex link");
    input.keys[VK_F6]=true;present();check(f.variable("enable_mods")==0,"Original key cycle disables the Core condition");
    draw(1);check(f.variable("hits")==2 && originals==1 && regions==4,"Disabled Core condition restores the original without resource matching");
    input.keys.fill(false);present();input.keys[VK_F6]=true;present();check(f.variable("enable_mods")==1,"Original key cycle restores the Core condition");
    call.call=DrawCallInfo{};call.call.type=DrawCall::Draw;call.call.VertexCount=3;draw(1);
    check(f.variable("hits")==2 && originals==2 && regions==4,"Core DRAW_TYPE excludes non-indexed calls");
    ib={};output.reset();f.release();
}
