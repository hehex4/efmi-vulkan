static frontend::SourceShaderState owned_shader_facts(const std::vector<gpu::HlslProgram>& programs){
    frontend::SourceShaderState facts;for(auto& stage:facts.stages)stage.known=true;
    for(const auto& program:programs){const unsigned i=program.stage==VK_SHADER_STAGE_VERTEX_BIT?0:program.stage==VK_SHADER_STAGE_FRAGMENT_BIT?4:5;
        facts.stages[i]={true,true,true,i+1,frontend::source_shader_declarations(program.dxbc.data(),program.dxbc.size())};}
    return facts;
}
static void original_shader_hooks(Device& d,const std::filesystem::path& root){
    const auto programs=ini_graphics_programs(root);
    for(unsigned mode=0;mode<6;++mode){const auto directory=root/std::to_string(mode);std::filesystem::create_directories(directory/"Mods");
        write(directory/"Mods/graphics.hlsl",ini_graphics_hlsl);
        std::string text=std::string(ini_graphics_resources)+R"ini(
[Constants]
global $inside_null = 0
global $inside_draw = 0
global $after = 0
global $posted = 0
[CustomShaderOuter]
ps = null
$inside_null = ps
run = CustomShaderDraw
$after = ps
[CustomShaderDraw]
vs = graphics.hlsl
ps = graphics.hlsl
$inside_draw = ps
draw = 3, 0
[CustomShaderFail]
vs = graphics.hlsl
ps = graphics.hlsl
topology = point_list
draw = 3, 0
[ShaderOverrideVertex]
hash = 1
x = 1
run = CustomShaderOuter
x = 2
)ini";
        if(mode==1)text+="handling = skip\n";
        text+="post x = 4\npost $posted = 1\npost run = "+std::string(mode==3?"CustomShaderFail":"CustomShaderDraw")+"\n";
        if(mode==2)text+="post handling = skip\n";
        text+="[ShaderOverridePixel]\nhash = 5\nfilter_index = 42\n";
        ShaderFixture f(d,directory,text.c_str(),true);f.backend->set_programs({programs.first,programs.second});
        auto state=frontend::source_graphics_defaults();state.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;state.raster.CullMode=D3D11_CULL_NONE;state.depth.DepthEnable=false;
        auto& blend=state.blend.RenderTarget[0];blend.BlendEnable=true;blend.SrcBlend=D3D11_BLEND_BLEND_FACTOR;blend.DestBlend=D3D11_BLEND_INV_BLEND_FACTOR;
        state.blend_factor.fill(.5f);f.backend->set_graphics_state(state);
        if(mode==0)refuses([&]{f.backend->set_programs({programs.first,programs.first});},"Duplicate source programs refuse without replacing current execution state");
        auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
        frontend::RuntimeShaderCall call;call.call.type=DrawCall::Draw;call.call.VertexCount=3;call.shaders=owned_shader_facts({programs.first,programs.second});
        if(mode==5){call.shaders.stages[0].hash=99;call.shaders.stages[4].hash=98;}
        unsigned originals=0;call.original=[&](CommandListState& current,std::wstring& why){++originals;
            if(mode==4){why=L"Intentional original operation refusal";return false;}return f.backend->execute_original(current,why);};
        f.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        const bool success=f.ini->intercept(call,f.why);
        if(mode==3 || mode==4){check(!success,"A failed post or original operation refuses the intercepted transaction");
            refuses([&]{f.backend->resources().finish();},"Failed intercepted transaction cannot submit its earlier recorded drawing");f.backend->resources().discard();
            const auto bytes=f.backend->resources().commands().readback(d.queue,output,image_region(output));
            check(std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0;}),"Failed intercepted work leaves committed pixels untouched");
            check(f.variable("posted")==float(mode==3),"Failed original operation never executes post roots");
        }else{if(!success)throw std::runtime_error(f.why);f.finish();
            const auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
            const auto expected=native_linked_graphics(programs.first,programs.second,{},[&](auto* device,auto* context){
                ImageNativeReference::Ptr<ID3D11BlendState> native_blend;check(SUCCEEDED(device->CreateBlendState(&state.blend,&native_blend)),"Native ordered shader-hook blend state");
                context->OMSetBlendState(native_blend.Get(),state.blend_factor.data(),UINT_MAX);
                if(mode!=5){native_ini_parameters(device,context,1);context->Draw(3,0);
                    if(mode!=1){native_ini_parameters(device,context,2);context->Draw(3,0);}
                    native_ini_parameters(device,context,4);
                }else native_ini_parameters(device,context,3);
            });
            check(actual==expected,"Vulkan pre/original/post drawing matches order-sensitive native DX11 pixels");
            check(f.backend->draws()==(mode==5?1:mode==1?2:3),"Pre skip suppresses exactly the original draw while post skip does not undo it");
            check(f.ini->status().shader_calls==1,"Successful owned source shader call is recorded once");
        }
        check(originals==(mode==1?0:1),"Original GPU boundary is invoked exactly once unless skipped");
        if(mode!=5)check(f.variable("inside_null")==42 && f.variable("inside_draw")==42 && f.variable("after")==42,
            "Shader queries retain original tracked identity through null and nested custom executables");
        output.reset();f.release();
    }
}
static void original_compute_hooks(Device& d,const std::filesystem::path& root){
    const auto programs=layout_programs(root);const auto original=std::make_shared<frontend::SourceInputLayout>(layout_elements(),programs[0].input_signature);
    const auto compute=compute_program(root/"Mods",R"hlsl(Texture1D<float4> Parameters:register(t120);
RWBuffer<uint> Output:register(u0);
[numthreads(1,1,1)] void main(){Output[0]=Output[0]*10+(uint)Parameters.Load(int2(0,0)).x;})hlsl");
    const std::string text=std::string(layout_resources)+R"ini(
[Constants]
cs-u0 = ResourceValue
[ResourceValue]
type = Buffer
format = R32_UINT
bind_flags = unordered_access shader_resource
data = R32_UINT 0
[CustomShaderCompute]
cs = compute.hlsl
dispatch = 1, 1, 1
[ShaderOverrideCompute]
hash = 6
vb0->elementoffset(ALPHA, 0) = 16
x = 1
run = CustomShaderCompute
x = 2
post x = 3
post run = CustomShaderCompute
)ini";
    ShaderFixture f(d,root,text.c_str(),true);f.backend->set_programs({compute});f.backend->set_input_layout(original);
    frontend::RuntimeShaderCall call;call.shaders=owned_shader_facts({compute});call.call.type=DrawCall::Dispatch;
    call.call.ThreadGroupCountX=call.call.ThreadGroupCountY=call.call.ThreadGroupCountZ=1;
    call.original=[&](CommandListState& state,std::wstring& why){return f.backend->execute_original(state,why);};
    f.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!f.ini->intercept(call,f.why))throw std::runtime_error(f.why);f.finish();
    auto output=f.backend->resources().binding(L'c',ResourceCopyTargetType::UNORDERED_ACCESS_VIEW,0).buffer;
    const auto bytes=f.backend->resources().commands().readback(d.queue,output,0,4);uint32_t value=0;memcpy(&value,bytes.data(),4);
    check(value==123 && f.backend->dispatches()==3,"Original compute dispatch runs between source pre/post GPU commands");
    check(f.backend->input_layout()!=original,"AfterDispatch preserves layout edits applied by injected commands");
    call.call=DrawCallInfo{};call.call.type=DrawCall::Draw;
    f.backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!f.ini->intercept(call,f.why))throw std::runtime_error(f.why);f.finish();
    check(f.backend->input_layout()==original,"The subsequent outer AfterDraw restores the original input layout");
    output.reset();f.release();
}
