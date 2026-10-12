// Included by asset_gpu.cpp. All allocations and readbacks use its own device.
static BufferUploadDispatch buffer_dispatch()
{
    BufferUploadDispatch d;
#define X(name) d.name=p##name;
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    return d;
}
static void finish_buffer(BufferUpload& upload,Gpu& g)
{
    if(!upload.record(g.family) || !upload.submit(g.queue))throw std::runtime_error(upload.reason());
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    while(upload.poll()==UploadState::Submitted && Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(upload.state()!=UploadState::Ready)throw std::runtime_error("Mesh upload did not complete: "+upload.reason());
}
static std::shared_ptr<BufferUpload> mesh_buffer(Gpu& g,const void* data,size_t size,VkBufferUsageFlags usage)
{
    auto upload=std::make_shared<BufferUpload>(g.device,buffer_dispatch());
    if(!upload->prepare(static_cast<const uint8_t*>(data),size,usage,g.memory))throw std::runtime_error(upload->reason());
    finish_buffer(*upload,g);return upload;
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
    for(size_t size:{1u,3u,6u,4101u}){
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
