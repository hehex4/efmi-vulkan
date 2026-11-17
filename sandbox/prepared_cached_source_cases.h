// Original cache views survive reference/copy and roll back with preparation.
static void prepared_cached_source_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"d3dx.ini",R"ini([Rendering]
cache_resource_data = constant_buffer
[Constants]
global $step = 0
global $direct = 0
global $saved = 0
global $copied = 0
global $region = 0
global $edge = 0
global $private = 0
global $chained = 0
[ResourceSaved]
[ResourceCopy]
[ResourceChained]
[ResourcePrivate]
type = Buffer
byte_width = 64
[Present]
$step = $step + 1
if $step < 3
 ResourceSaved = ref vs-cb0
 ResourceCopy = copy vs-cb0
 ResourceChained = copy ResourceCopy
endif
$direct = vs-cb0->SpatialHash(0,1,2,1)
$saved = ResourceSaved->SpatialHash(0,1,2,1)
$copied = ResourceCopy->SpatialHash(0,1,2,1)
$region = ResourceSaved->HashRegion(0,128)
$edge = ResourceSaved->SpatialHash(5,6,7,1)
$private = ResourcePrivate->HashRegion(0,16)
$chained = ResourceChained->SpatialHash(0,1,2,1)
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    auto gpu=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=gpu->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-cached-source"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},gpu),why);
    if(!ini)throw std::runtime_error("Load cached source registry: "+why);
    check(ini->status().diagnostics==0,"Cached-source fixture has no original parser diagnostics");
    {auto submission=resources.finish();if(submission)submission->wait();}
    const float values[]={99,98,97,96,1.5f,-2.25f,4.75f,95,3,4,5,94,6,7,8,93};
    auto bytes=std::make_shared<std::vector<uint8_t>>(sizeof(values));memcpy(bytes->data(),values,sizeof(values));
    gpu::ResourceCommands app(d.resource);auto buffer=app.create(sizeof(values),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,values,sizeof(values));
    {auto submission=app.submit(d.queue);submission->wait();}
    Runtime::ApplicationResourceState state;state.omitted_slots_unbound=true;
    Runtime::ApplicationBufferBinding binding;binding.stage=L'v';binding.type=Target::CONSTANT_BUFFER;
    binding.source_known=true;binding.description={sizeof(values),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};
    auto& r=binding.resource;r.bound=true;r.dimension=D3D11_RESOURCE_DIMENSION_BUFFER;r.identity=101;
    r.buffer=reinterpret_cast<uint64_t>(buffer->buffer());r.size=sizeof(values);r.offset=16;r.range=32;r.usage=buffer->usage();
    r.memory=reinterpret_cast<uint64_t>(buffer->memory());r.memory_offset=buffer->memory_offset();r.memory_size=buffer->memory_size();r.keep=buffer;
    auto facts=std::make_shared<frontend::SourceResourceIdentity>();facts->tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
    facts->cached_data=[bytes]{return bytes;};binding.identity=facts;state.buffers.push_back(binding);
    auto run=[&]{resources.begin_prepared(state);check(ini->present({},10,0,why) && !resources.failed(),"Original cached-source queries prepare successfully");};
    auto bits=[](float value){uint32_t word;memcpy(&word,&value,4);return word;};
    // Original PackCellCoords: floor and modulo, X:12 / Y:8 / Z:12 bits.
    constexpr uint32_t expected=(1u<<20)|(253u<<12)|4u;
    run();auto status=ini->status();
    check(bits(status.variables.at("$direct"))==expected,"Application CB spatial hash reads its original offset in float units");
    check(bits(status.variables.at("$saved"))==expected && bits(status.variables.at("$copied"))==expected,
        "Reference and copy declarations retain the original cached data window");
    uint32_t crc=efvk::crc32c(0,bytes->data()+16,32);crc^=crc>>16;crc*=0x85ebca6bu;crc^=crc>>13;
    const uint32_t encoded=((1u+((crc&0x3fffffffu)>>23))<<23)|(crc&0x7fffffu);
    check(bits(status.variables.at("$region"))==encoded,"RegionHash clamps the request to the cached source window");
    check(status.variables.at("$edge")==-1 && status.variables.at("$private")==-1 && status.variables.at("$chained")==-1,
        "Original strict spatial bound and uncached private/chained resources retain UNKNOWN");
    resources.finish_prepared();resources.accept_prepared();
    auto changed=std::make_shared<std::vector<uint8_t>>(*bytes);const float shifted=10;memcpy(changed->data()+16,&shifted,4);
    facts->cached_data=[changed]{return changed;};run();
    check(bits(ini->status().variables.at("$saved"))!=expected,"A fresh original assignment acquires the new source cache");
    resources.finish_prepared();resources.discard();run();
    check(bits(ini->status().variables.at("$saved"))==expected,"Cancelled assignment restores the retained earlier cached window");
    resources.finish_prepared();resources.discard();
}
