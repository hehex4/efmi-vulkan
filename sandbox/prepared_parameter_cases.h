static void prepared_parameter_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"parameters.hlsl",R"hlsl(Texture1D<float4> Ini : register(t120);
Texture2D<uint> Source : register(t0);
RWStructuredBuffer<float> Result : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Result[id.x] = Source.Load(int3(id.xy,0)) + Ini.Load(0).x; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $step = 0
global $feedback = -1
x = 1
[ResourceTexture]
type = Texture2D
format = R32_UINT
width = 4
height = 1
array = 1
mips = 1
msaa = 1
bind_flags = shader_resource unordered_access
[ResourceSaved]
bind_flags = shader_resource
[ResourceOutput]
type = StructuredBuffer
stride = 4
byte_width = 16
bind_flags = unordered_access
[CustomShaderRead]
cs = parameters.hlsl
cs-t0 = ref ResourceTexture
cs-t120 = ref iniparams
cs-u0 = ref ResourceOutput
dispatch = 4,1,1
[Present]
$step = $step + 1
if $step <= 2
 clear = ResourceTexture 10
 x = 2
 run = CustomShaderRead
 ResourceSaved = copy ResourceTexture
elif $step == 3
 clear = ResourceTexture 20
 x = 5
 run = CustomShaderRead
elif $step == 4 || $step == 6 || $step == 10
 run = CustomShaderRead
elif $step == 5
 ResourceSaved = null
 clear = ResourceTexture 31
 x = 7
 run = CustomShaderRead
 store = $feedback, ResourceOutput, 0
elif $step == 7
 cs-t120 = null
 x = 8
elif $step == 8
 clear = ResourceTexture 41
 ResourceSaved = copy ResourceTexture
elif $step == 9
 x = 9
endif
cs-u0 = ref ResourceOutput
ps-t1 = ref ResourceSaved
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    gpu::ResourceBudgets budgets;
    auto gpu=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets);
    auto& resources=gpu->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-parameters"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},gpu),why);
    if(!ini)throw std::runtime_error("Load prepared parameters: "+why);++checks;
    for(const auto& gap:ini->status().gaps)fprintf(stderr,"Prepared parameter load: %s\n",gap.c_str());
    check(ini->status().gaps.empty(),"Prepared parameter shader loads without adapter gaps");
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    const auto initial_uploads=resources.parameter_uploads();
    auto parameter=[&]{auto bytes=resources.commands().readback(d.queue,resources.ini_parameters().image,image_region(resources.ini_parameters().image));float value=0;memcpy(&value,bytes.data(),4);return value;};
    auto result=[&](float value){auto binding=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0);if(!binding.buffer)return false;
        auto bytes=resources.commands().readback(d.queue,binding.buffer,0,16);float values[4];memcpy(values,bytes.data(),16);
        const bool matched=std::all_of(std::begin(values),std::end(values),[&](float x){return x==value;});
        if(!matched)fprintf(stderr,"Prepared result expected %g, actual %g %g %g %g\n",double(value),double(values[0]),double(values[1]),double(values[2]),double(values[3]));return matched;};
    auto saved=[&](uint32_t value){auto binding=resources.binding(L'p',Target::SHADER_RESOURCE,1);if(!binding.image)return false;
        auto bytes=resources.commands().readback(d.queue,binding.image,image_region(binding.image));uint32_t values[4];memcpy(values,bytes.data(),16);
        return std::all_of(std::begin(values),std::end(values),[&](uint32_t x){return x==value;});};
    unsigned frame=0;
    auto begin=[&]{resources.begin_prepared();check(ini->present({},++frame,0,why),"Original image and parameter commands evaluate in preparation");};
    auto cancel=[&]{check(!resources.failed(),"Prepared image scope is admissible before cancellation");auto packet=resources.finish_prepared();resources.discard();};
    auto admit=[&]{check(!resources.failed(),"Prepared image scope admits without contained failures");auto packet=resources.finish_prepared();resources.accept_prepared();record_prepared_packets(d,{packet});};
    check(parameter()==1,"Initial original parameter upload completed before preparation");
    begin();cancel();check(resources.parameter_uploads()==initial_uploads && parameter()==1,"Cancelled parameter publication preserves GPU bytes and committed upload count");
    begin();admit();check(parameter()==2,"Unchanged CPU parameter retries publication at its original boundary");
    check(result(12),"Original image compute consumes the retried parameter publication");
    check(saved(10),"Original image copy sees the ordered clear");
    const auto admitted_uploads=resources.parameter_uploads();begin();cancel();
    check(resources.parameter_uploads()==admitted_uploads && parameter()==2 && result(12) && saved(10),"Cancelled image write and parameter dispatch preserve admitted data");
    begin();admit();check(parameter()==5 && result(15),"A later unchanged parameter republishes after cancelled compute");
    begin();check(resources.failed(),"Unsupported feedback rejects the entire image scope after GPU preparation");
    refuses([&]{resources.finish_prepared();},"Failed image scope cannot seal partial work");resources.discard();
    check(parameter()==5 && result(15) && saved(10) && ini->status().variables.at("$feedback")==-1,"Failed scope restores image aliases and discards GPU writes");
    begin();admit();check(parameter()==7 && result(17),"Failed-scope parameters retry at the next original flush boundary");
    begin();admit();check(parameter()==8 && !resources.binding(L'c',Target::SHADER_RESOURCE,120),"Parameter publication does not rebind an explicitly cleared source slot");
    begin();cancel();check(saved(10) && parameter()==8,"Cancelled image copy-cache changes preserve the admitted named image");
    const auto pressure=budgets.staging->limit()-budgets.staging->used();
    check(budgets.staging->acquire(pressure),"Reserve upload pressure before the next parameter flush");
    resources.begin_prepared();const bool evaluated=ini->present({},++frame,0,why);
    check(!evaluated && resources.failed(),"Parameter allocation failure fails the original root boundary");
    refuses([&]{resources.finish_prepared();},"A failed parameter upload cannot seal");resources.discard();budgets.staging->release(pressure);
    check(parameter()==8 && saved(10),"Parameter allocation failure leaves committed GPU state intact");
    begin();admit();check(parameter()==9 && result(19),"An unchanged CPU parameter retries after staging allocation failure");
}
