// Included by asset_gpu.cpp. All allocations and readbacks use its own device.
static BufferUploadDispatch buffer_dispatch()
{
    BufferUploadDispatch d;
#define X(name) d.name=p##name;
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    return d;
}
static void buffer_roundtrip(Gpu& g,std::vector<uint8_t> bytes)
{
    const auto expected=bytes;auto resident=std::make_shared<AllocationBudget>(160ull<<20),staging=std::make_shared<AllocationBudget>(160ull<<20);
    {
        BufferUpload upload(g.device,buffer_dispatch(),resident,staging);
        const VkBufferUsageFlags usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        check(upload.prepare(bytes.data(),bytes.size(),usage,g.memory,true) && !upload.buffer() && resident->used() && staging->used(),
              "mesh preparation owns exact source bytes but exposes no buffer");
        if(upload.state()!=UploadState::Prepared)throw std::runtime_error(upload.reason());
        uint32_t lower=17,upper=19;
        check(!upload.index_extents(VK_INDEX_TYPE_UINT16,lower,upper) && lower==17 && upper==19,
              "index extrema are not published before upload readiness");
        std::fill(bytes.begin(),bytes.end(),0xcc);bytes.clear();bytes.shrink_to_fit();
        check(!upload.submit(g.queue) && upload.state()==UploadState::Prepared,"unrecorded mesh ticket cannot submit");
        check(upload.record(g.family) && !upload.buffer() && !upload.submit(VK_NULL_HANDLE) && upload.state()==UploadState::Recorded,
              "recorded upload needs a real caller queue and remains hidden");
        check(upload.submit(g.queue) && !upload.buffer() && upload.state()==UploadState::Submitted,"submitted mesh stays unpublished before polling");
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(upload.poll()==UploadState::Submitted && Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(upload.buffer() && upload.size()==expected.size() && upload.usage()==usage && !staging->used() && resident->used(),
              "fence completion publishes mesh and frees staging charge");
        if(!upload.buffer())throw std::runtime_error(upload.reason());
        for(auto type:{VK_INDEX_TYPE_UINT16,VK_INDEX_TYPE_UINT32,VK_INDEX_TYPE_MAX_ENUM}){
            const size_t width=type==VK_INDEX_TYPE_UINT16?2:type==VK_INDEX_TYPE_UINT32?4:0;
            const bool valid=width && !(expected.size()%width);
            uint32_t low=UINT32_MAX,high=0;
            if(valid)for(size_t i=0;i<expected.size();i+=width){uint32_t value=0;memcpy(&value,expected.data()+i,width);
                low=std::min(low,value);high=std::max(high,value);}
            lower=17;upper=19;const bool available=upload.index_extents(type,lower,upper);
            check(available==valid && (valid?(lower==low && upper==high):(lower==17 && upper==19)),
                  "typed extrema match original uploaded bytes after source destruction or reject partial index elements");
        }
        check(!upload.prepare(expected.data(),expected.size(),usage,g.memory) && !upload.record(g.family) && !upload.submit(g.queue) && upload.buffer(),
              "ready immutable mesh ticket cannot be re-prepared recorded or submitted");
        auto out=g.readback(expected.size());auto cmd=g.begin();
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.buffer=upload.buffer();barrier.size=upload.size();
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        pCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        VkBufferCopy copy{0,0,upload.size()};pCmdCopyBuffer(cmd,upload.buffer(),out.buffer,1,&copy);g.finish(cmd,out);
        check(!memcmp(out.mapped,expected.data(),expected.size()),"every mesh byte survives source destruction and device-local upload");
    }
    check(!resident->used() && !staging->used(),"mesh owner releases actual allocation charges");
}
static VKAPI_ATTR VkResult VKAPI_CALL buffer_submit_failure(VkQueue,uint32_t,const VkSubmitInfo*,VkFence)
{return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
static void buffer_tests(Gpu& g)
{
    for(size_t size:{1u,3u,6u,8u,4101u}){
        std::vector<uint8_t> data(size);for(size_t i=0;i<size;++i)data[i]=static_cast<uint8_t>(i*17+31);
        buffer_roundtrip(g,std::move(data));
    }
    auto d=buffer_dispatch();uint8_t data[8]={};
    for(auto usage:{0u,uint32_t(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),uint32_t(VK_BUFFER_USAGE_TRANSFER_DST_BIT)}){
        BufferUpload invalid(g.device,d);check(!invalid.prepare(data,sizeof(data),usage,g.memory) && !invalid.buffer(),"unsupported mesh usages refuse before allocation");
    }
    for(unsigned invalid: {0u,1u,2u}){
        BufferUpload upload(g.device,d);
        check(!upload.prepare(invalid==0?nullptr:data,invalid==1?0:(invalid==2?(128ull<<20)+1:sizeof(data)),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,g.memory),
              "null empty and over-budget mesh sources refuse");
    }
    auto resident=std::make_shared<AllocationBudget>(160ull<<20),staging=std::make_shared<AllocationBudget>(160ull<<20);
    {
        BufferUpload cancelled(g.device,d,resident,staging);
        check(cancelled.prepare(data,sizeof(data),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,g.memory) && cancelled.record(g.family),"mesh cancellation fixture records without submission");
    }
    check(!resident->used() && !staging->used(),"unsubmitted cancellation releases all allocations");
    {
        d.QueueSubmit=buffer_submit_failure;BufferUpload failed(g.device,d,resident,staging);
        check(failed.prepare(data,sizeof(data),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,g.memory) && failed.record(g.family) &&
              !failed.submit(g.queue) && failed.state()==UploadState::Failed && !failed.buffer(),"failed submission never publishes a mesh");
    }
    check(!resident->used() && !staging->used(),"failed mesh submission cleans both allocation ledgers");
    for(bool resident_limit:{true,false}){
        auto tiny=std::make_shared<AllocationBudget>(1);
        {BufferUpload upload(g.device,buffer_dispatch(),resident_limit?tiny:resident,resident_limit?staging:tiny);
         check(!upload.prepare(data,sizeof(data),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,g.memory),"actual mesh allocation requirements enforce aggregate budgets");}
        check(!tiny->used() && !resident->used() && !staging->used(),"budget refusal leaves no retained mesh allocation charge");
    }
}

static void resource_cache_tests(Gpu& g)
{
    const auto folder=std::filesystem::current_path()/L"cache-fixtures";
    const auto path=folder/L"mesh.buf",texture=folder/L"mesh-texture.dds",missing=folder/L"unused-mesh.buf";
    std::error_code ignored;std::filesystem::remove(missing,ignored);
    const float positions[]={-.8f,-.8f,-.2f,-.8f,-.5f,-.2f};
    std::vector<uint8_t> bytes(sizeof(positions));memcpy(bytes.data(),positions,sizeof(positions));
    write_fixture(path,bytes);write_fixture(texture,fixture(99));
    ResourceBudgets budget;ResourceCache cache(cache_device(g),budget,8);std::string reason;
    auto a=cache.declare_buffer(path,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,reason);
    auto b=cache.declare_buffer(path,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,reason);
    auto image=cache.declare_image(path,reason),real_image=cache.declare_image(texture,reason);
    auto dormant=cache.declare_buffer(missing,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,reason);
    check(a && b && image && a!=b && a!=image && cache.declare_buffer(folder/L"."/L"mesh.buf",VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,reason)==a,
          "resource cache keys include normalized path and buffer usage or image kind");
    check(dormant && dormant->snapshot().state==ResourceState::Dormant && !cache.stats().file_reads,
          "unused missing mesh declarations remain dormant");
    check(!cache.declare_buffer(path,0,reason) && !cache.declare_buffer(path,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,reason),
          "unsupported buffer usage cannot alias image or another buffer request");
    std::vector<std::thread> callers;std::atomic<unsigned> accepted{0};
    for(unsigned i=0;i<4;++i)callers.emplace_back([&]{for(unsigned j=0;j<32;++j)if(cache.demand(a))++accepted;});
    for(auto& t:callers)t.join();
    cache.demand(real_image);cache.demand(b);cache.demand(image);
    auto first=await_texture(cache,a,g,false);
    check(accepted==128 && first.state==ResourceState::Prepared && !first.buffer && !first.image && cache.stats().file_reads==1,
          "mixed queue retains one prepared upload and deduplicates mesh demand");
    auto ready=await_texture(cache,a,g,true);auto ready_image=await_texture(cache,real_image,g,true);
    auto second=await_texture(cache,b,g,true);auto bad_image=await_texture(cache,image,g,true);
    check(ready.buffer && !ready.image && ready.buffer->size()==bytes.size() && ready.buffer->usage()==VK_BUFFER_USAGE_VERTEX_BUFFER_BIT &&
          second.buffer && second.buffer->usage()==VK_BUFFER_USAGE_INDEX_BUFFER_BIT && second.buffer!=ready.buffer,
          "mesh readiness preserves typed usage and independent owners");
    check(ready_image.image && !ready_image.buffer && bad_image.state==ResourceState::Failed && !bad_image.image && !bad_image.buffer,
          "mixed queue decodes images and rejects mesh bytes requested as DDS");
    check(cache.stats().file_reads==4 && cache.stats().submissions==3 && cache.stats().ready==3 && cache.stats().failed==1,
          "mixed resource outcomes and submissions account exactly once");
    auto retained=ready.buffer;const auto old_bytes=budget.resident->used();
    cache.stop();ready={};second={};ready_image={};
    check(retained->buffer() && budget.resident->used() && budget.resident->used()<old_bytes && !budget.staging->used() && !budget.source->used(),
          "retired mixed generation releases unretained images but preserves command-owned mesh charges");
    ResourceCache next(cache_device(g),budget);auto again=next.declare_buffer(path,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,reason);
    bytes.resize(bytes.size()+8);write_fixture(path,bytes);next.demand(again);
    auto changed=await_texture(next,again,g,true);
    check(changed.buffer && changed.buffer->size()==bytes.size() && retained->size()==sizeof(positions) && !next.demand(a),
          "new generation owns changed mesh bytes while old generation and foreign handles stay distinct");
    next.stop();changed={};retained.reset();
    check(!budget.resident->used() && !budget.staging->used(),"last mixed-generation command owner releases shared allocation budgets");
    check(!wrong_worker_calls && !wrong_submit_calls,"mesh and image creation/polling stay on worker and queue submission on caller");
}

static void resource_sample(Gpu& g,int count,wchar_t** paths)
{
    ResourceBudgets budget;ResourceCache cache(cache_device(g),budget);std::string reason;
    std::vector<ResourceHandle> requests;
    for(int i=0;i<count;++i){
        const std::filesystem::path path(paths[i]);
        auto request=path.extension()==L".dds"?cache.declare_image(path,reason):
                     cache.declare_buffer(path,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,reason);
        if(!request)throw std::runtime_error(reason);requests.push_back(request);cache.demand(request);
    }
    std::vector<ResourceSnapshot> retained;
    for(const auto& request:requests){
        auto value=await_texture(cache,request,g,true);if(value.state!=ResourceState::Ready)throw std::runtime_error(value.reason);
        retained.push_back(std::move(value));
    }
    const auto stats=cache.stats();
    check(stats.ready==requests.size() && stats.file_reads==requests.size() && stats.submissions==requests.size() && !stats.failed,
          "all selected sample mesh and image resources share one bounded queue");
    cache.stop();
    check(budget.resident->used() && !budget.staging->used() && !budget.source->used(),"sample ready owners survive shared cache retirement");
    printf("sample resources: count=%zu resident_peak=%llu staging_peak=%llu source_peak=%llu\n",retained.size(),
           static_cast<unsigned long long>(budget.resident->peak()),static_cast<unsigned long long>(budget.staging->peak()),
           static_cast<unsigned long long>(budget.source->peak()));
    retained.clear();check(!budget.resident->used(),"sample final owners release all mixed residency");
    check(!wrong_worker_calls && !wrong_submit_calls,"sample mixed queue keeps driver calls on intended threads");
}
