// Mutable image byte/pixel results and failure/lifetime tests. Included by the
// headless resource runner; no original game images or device are imported.
#include "../frontend/gpu/image_formats.h"
#include <algorithm>
static_assert(!std::is_copy_constructible_v<gpu::ImageResource> && !std::is_copy_assignable_v<gpu::ImageViewResource>,"Images and views have unique Vulkan owners");
static VkBufferImageCopy image_region(const gpu::Image& image,uint32_t mip=0,uint32_t layer=0,uint32_t layers=1,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
    VkBufferImageCopy r{};r.imageSubresource={aspect,mip,layer,layers};r.imageExtent=image->extent(mip);return r;
}
static VkImageCopy image_copy_region(const gpu::Image& image,uint32_t layers=1,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageCopy r{};r.srcSubresource=r.dstSubresource={aspect,0,0,layers};r.extent=image->extent();return r;
}
static PFN_vkQueueSubmit real_image_submit=nullptr;
static bool fail_image_submit=false;
static VKAPI_ATTR VkResult VKAPI_CALL image_submit(VkQueue q,uint32_t count,const VkSubmitInfo* submits,VkFence fence) {
    if(fail_image_submit){fail_image_submit=false;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    return real_image_submit(q,count,submits,fence);
}
static void image_commands(Device& d) {
    gpu::ResourceBudgets budgets;
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;
        desc.type=VK_IMAGE_TYPE_1D;desc.format=VK_FORMAT_R32_UINT;desc.extent={8,1,1};desc.mips=4;desc.layers=3;
        desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
        auto line=c.create_image(desc);VkClearColorValue zero{};c.clear(line,line->full_range(),zero);
        VkClearColorValue number{};number.uint32[0]=77;c.clear(line,{VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,1},number);
        auto bytes=c.readback(d.queue,line,image_region(line,1,0,3));std::vector<uint32_t> expected(12,0);
        std::fill(expected.begin()+4,expected.begin()+8,77);
        check(bytes.size()==expected.size()*4 && !memcmp(bytes.data(),expected.data(),bytes.size()),"1D array clear selects mip/layer and preserves other layers");
        bytes=c.readback(d.queue,line,image_region(line,0,0,3));check(std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0;}),"Clearing a mip preserves the base level");
        const uint32_t tail=0xdeadbeef;auto r=image_region(line,3,2);c.write(line,r,&tail,sizeof(tail));
        bytes=c.readback(d.queue,line,r);check(bytes.size()==4 && !memcmp(bytes.data(),&tail,4),"Smallest array mip host upload");
        auto array=c.view(line,VK_IMAGE_VIEW_TYPE_1D_ARRAY,VK_FORMAT_UNDEFINED,line->full_range());
        auto one=c.view(line,VK_IMAGE_VIEW_TYPE_1D,VK_FORMAT_UNDEFINED,{VK_IMAGE_ASPECT_COLOR_BIT,2,VK_REMAINING_MIP_LEVELS,1,1});
        check(array->range().layerCount==3 && one->range().levelCount==2 && one->owner()==line,"Owned image views preserve normalized subresource ranges");
        refuses([&]{c.view(line,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_UNDEFINED,line->full_range());},"Image view dimensional mismatch refused");
        refuses([&]{c.view(line,VK_IMAGE_VIEW_TYPE_1D,VK_FORMAT_UNDEFINED,line->full_range());},"Non-array view rejects several layers");
        refuses([&]{c.view(line,VK_IMAGE_VIEW_TYPE_1D_ARRAY,VK_FORMAT_R32_SFLOAT,line->full_range());},"Reinterpreted view needs mutable allocation");
        refuses([&]{c.clear(line,{VK_IMAGE_ASPECT_COLOR_BIT,4,1,0,1},zero);},"Out-of-bounds mip refused");
        refuses([&]{c.clear(line,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,2,2},zero);},"Out-of-bounds layer range refused");
        refuses([&]{c.clear(line,{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1},VkClearDepthStencilValue{});},"Wrong image aspect refused");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_R8G8B8A8_UINT;
        desc.extent={7,5,1};desc.layers=3;desc.mips=3;desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        auto image=c.create_image(desc);VkClearColorValue color{};color.uint32[0]=1;color.uint32[1]=2;color.uint32[2]=3;color.uint32[3]=4;
        c.clear(image,image->full_range(),color);auto region=image_region(image,0,1,2);
        region.imageOffset={2,1,0};region.imageExtent={3,2,1};region.bufferRowLength=5;region.bufferImageHeight=3;
        check(c.transfer_size(image,region)==92,"Padded array transfer size ends at last written texel");
        std::vector<uint8_t> upload(92,0xcd),expected(7*5*3*4);
        for(size_t i=0;i<expected.size();++i)expected[i]=uint8_t(i%4+1);
        for(unsigned layer=0;layer<2;++layer)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x)for(unsigned k=0;k<4;++k) {
            uint8_t v=uint8_t(10+layer*30+y*12+x*4+k);upload[(layer*15+y*5+x)*4+k]=v;
            expected[(((layer+1)*5+(y+1))*7+x+2)*4+k]=v;
        }
        c.write(image,region,upload.data(),upload.size());auto bytes=c.readback(d.queue,image,image_region(image,0,0,3));
        check(bytes==expected,"Padded upload writes correct rows/layers and preserves pixels outside region");
        auto destination=c.create_image(desc);c.clear(destination,destination->full_range(),color);
        VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,2};copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};
        copy.srcOffset=region.imageOffset;copy.extent=region.imageExtent;c.copy(image,destination,copy);
        auto part=image_region(destination,0,0,2);part.imageExtent=region.imageExtent;bytes=c.readback(d.queue,destination,part);
        std::vector<uint8_t> tight;for(unsigned l=0;l<2;++l)for(unsigned y=0;y<2;++y)tight.insert(tight.end(),upload.begin()+(l*15+y*5)*4,upload.begin()+(l*15+y*5+3)*4);
        check(bytes==tight,"Image-to-image region copy preserves array layer selection");
        region.bufferOffset=16;auto buffer=c.create(128,0,nullptr,0,true);c.copy(image,buffer,region);
        bytes=c.readback(d.queue,buffer,0,128);std::vector<uint8_t> buffered(128,0);
        for(unsigned l=0;l<2;++l)for(unsigned y=0;y<2;++y)memcpy(buffered.data()+16+(l*15+y*5)*4,upload.data()+(l*15+y*5)*4,12);
        check(bytes==buffered,"Image download honors buffer offset and leaves row/slice padding untouched");
        c.copy(buffer,image,region);check(c.readback(d.queue,image,image_region(image,0,0,3))==expected,"Explicit buffer-image upload uses identical pitches");
        desc.format=VK_FORMAT_R32_UINT;auto words=c.create_image(desc);c.copy(image,words,image_copy_region(image,3));
        check(c.readback(d.queue,words,image_region(words,0,0,3))==expected,"Size-compatible color copy preserves bits across formats");
        auto bad=region;bad.bufferRowLength=2;refuses([&]{c.copy(buffer,image,bad);},"Small row pitch refused");
        bad=region;bad.bufferOffset=2;refuses([&]{c.copy(buffer,image,bad);},"Misaligned buffer offset refused");
        bad=region;bad.imageOffset.x=6;refuses([&]{c.copy(buffer,image,bad);},"Region overflowing image refused");
        bad=region;bad.bufferRowLength=UINT32_MAX;refuses([&]{c.copy(buffer,image,bad);},"Overflowing transfer row pitch refused");
        auto short_buffer=c.create(4,0);refuses([&]{c.copy(image,short_buffer,region);},"Small image download buffer refused");
        refuses([&]{c.copy(image,image,image_copy_region(image));},"Overlapping same-image copy refused");
        refuses([&]{c.write(image,region,upload.data(),upload.size());},"Host image upload rejects nonzero source offset");
        region.bufferOffset=0;refuses([&]{c.write(image,region,upload.data(),upload.size()-1);},"Truncated host image upload refused");
        gpu::ResourceCommands other(d.resource,budgets);refuses([&]{other.clear(image,image->full_range(),color);},"Foreign image cannot cross execution domain");
        auto foreign=other.create(128,0);refuses([&]{c.copy(image,foreign,region);},"Foreign buffer cannot enter image transfer");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_R32G32B32A32_SFLOAT;
        desc.extent={3,2,1};desc.usage=VK_IMAGE_USAGE_STORAGE_BIT;
        auto image=c.create_image(desc);VkClearColorValue color{};const float values[]={1.25f,-2.0f,3.75f,4.5f};memcpy(color.float32,values,sizeof(values));
        c.clear(image,image->full_range(),color);const auto bytes=c.readback(d.queue,image,image_region(image));
        bool okay=bytes.size()==6*sizeof(values);for(size_t i=0;i<6 && okay;++i)okay=!memcmp(bytes.data()+i*sizeof(values),values,sizeof(values));
        check(okay,"Typed float image clear preserves all four independent components");
        auto view=c.view(image,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_UNDEFINED,image->full_range());std::weak_ptr<gpu::ImageResource> weak=image;
        image.reset();check(!weak.expired(),"A standalone image view retains its allocation after transfer completion");
        view.reset();check(weak.expired(),"Destroying the last image view releases its allocation");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.type=VK_IMAGE_TYPE_3D;
        desc.format=VK_FORMAT_R16_UINT;desc.extent={4,3,3};desc.mips=3;desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        auto volume=c.create_image(desc);std::vector<uint16_t> voxels(36);for(unsigned i=0;i<voxels.size();++i)voxels[i]=uint16_t(i+100);
        c.write(volume,image_region(volume),voxels.data(),voxels.size()*2);auto copy=image_copy_region(volume);copy.extent.depth=1;copy.dstOffset.z=2;
        c.copy(volume,volume,copy);std::copy(voxels.begin(),voxels.begin()+12,voxels.begin()+24);
        auto bytes=c.readback(d.queue,volume,image_region(volume));check(bytes.size()==voxels.size()*2 && !memcmp(bytes.data(),voxels.data(),bytes.size()),"Non-overlapping volume self-copy preserves other slices");
        auto view=c.view(volume,VK_IMAGE_VIEW_TYPE_3D,VK_FORMAT_UNDEFINED,volume->full_range());check(view->range().levelCount==3,"3D image view keeps complete mip chain");
        auto mip=image_region(volume,1);const uint16_t mip_data[]={0x1234,0xabcd};c.write(volume,mip,mip_data,sizeof(mip_data));
        bytes=c.readback(d.queue,volume,mip);check(bytes.size()==4 && !memcmp(bytes.data(),mip_data,4),"Volume mip dimensions shrink independently");
        auto bad=desc;bad.layers=2;refuses([&]{c.create_image(bad);},"3D array allocation refused");
    }
    {
        check(d.resource.enabled_features.textureCompressionBC,"BC feature available for compressed image tests");
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        desc.extent={7,5,1};desc.layers=2;desc.mips=3;desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;auto source=c.create_image(desc);auto target=c.create_image(desc);
        for(unsigned mip=0;mip<3;++mip) {
            auto r=image_region(source,mip,0,2);std::vector<uint8_t> bytes(size_t(c.transfer_size(source,r)));
            for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i*7+mip*19);
            c.write(source,r,bytes.data(),bytes.size());VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource=r.imageSubresource;copy.extent=r.imageExtent;
            c.copy(source,target,copy);check(c.readback(d.queue,target,r)==bytes,"Compressed edge and small-mip copies preserve complete blocks");
        }
        auto bad=image_region(source);bad.imageOffset.x=1;bad.imageExtent.width=4;refuses([&]{c.transfer_size(source,bad);},"Unaligned compressed offset refused");
        bad=image_region(source);bad.imageExtent.width=2;refuses([&]{c.transfer_size(source,bad);},"Partial compressed block must end at image edge");
        bad=image_region(source);bad.bufferRowLength=7;refuses([&]{c.transfer_size(source,bad);},"Compressed row pitch must be block aligned");
        refuses([&]{c.clear(source,source->full_range(),VkClearColorValue{});},"Compressed color clear refused before Vulkan call");
        desc.format=VK_FORMAT_R8G8B8A8_UNORM;desc.extent={8,8,1};desc.layers=12;desc.mips=4;
        desc.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT|VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;auto cube=c.create_image(desc);
        auto first=c.view(cube,VK_IMAGE_VIEW_TYPE_CUBE,VK_FORMAT_R8G8B8A8_SRGB,{VK_IMAGE_ASPECT_COLOR_BIT,0,4,0,6});
        auto second=c.view(cube,VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,VK_FORMAT_UNDEFINED,cube->full_range());
        check(first->format()==VK_FORMAT_R8G8B8A8_SRGB && second->range().layerCount==12,"Mutable cube and cube-array views retain correct format/range");
        const uint8_t pixel[]={1,2,3,4};c.write(cube,image_region(cube,3,11),pixel,4);
        check(c.readback(d.queue,cube,image_region(cube,3,11))==std::vector<uint8_t>(pixel,pixel+4),"Last cube-array face mip upload/readback");
        refuses([&]{c.view(cube,VK_IMAGE_VIEW_TYPE_CUBE,VK_FORMAT_R16G16B16A16_SFLOAT,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6});},"Mutable image still refuses incompatible view class");
        auto invalid=desc;invalid.extent.height=4;refuses([&]{c.create_image(invalid);},"Non-square cube refused");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_D32_SFLOAT_S8_UINT;
        desc.extent={4,3,1};desc.layers=2;desc.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        auto depth=c.create_image(desc),other=c.create_image(desc);c.clear(depth,depth->full_range(),VkClearDepthStencilValue{0.25f,0x5a});
        c.clear(depth,{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,1,1},VkClearDepthStencilValue{0.75f,0});
        c.copy(depth,other,image_copy_region(depth,2,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT));
        auto bytes=c.readback(d.queue,other,image_region(other,0,0,2,VK_IMAGE_ASPECT_DEPTH_BIT));
        bool okay=bytes.size()==24*4;for(unsigned i=0;i<24 && okay;++i){float v;memcpy(&v,bytes.data()+i*4,4);okay=v==(i<12?0.25f:0.75f);}
        check(okay,"Depth-only clear and combined aspect copy preserve array depth values");
        bytes=c.readback(d.queue,other,image_region(other,0,0,2,VK_IMAGE_ASPECT_STENCIL_BIT));
        check(bytes.size()==24 && std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0x5a;}),"Depth-only clear preserves independently transferred stencil bytes");
        auto srv=c.view(other,VK_IMAGE_VIEW_TYPE_2D_ARRAY,VK_FORMAT_UNDEFINED,{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,2},VK_IMAGE_USAGE_SAMPLED_BIT);
        auto dsv=c.view(other,VK_IMAGE_VIEW_TYPE_2D_ARRAY,VK_FORMAT_UNDEFINED,other->full_range(),VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        check(srv->range().aspectMask==VK_IMAGE_ASPECT_DEPTH_BIT && dsv->range().aspectMask==6,"Depth sampling and attachment views retain distinct aspects/usages");
        refuses([&]{c.clear(depth,depth->full_range(),VkClearDepthStencilValue{-1,0});},"Depth clear below enabled range refused");
        refuses([&]{c.clear(depth,depth->full_range(),VkClearDepthStencilValue{std::numeric_limits<float>::quiet_NaN(),0});},"Nonfinite depth clear refused");
        auto both=image_region(other,0,0,1,6);refuses([&]{c.transfer_size(other,both);},"Buffer transfer cannot combine depth and stencil planes");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_R8G8B8A8_UNORM;
        desc.extent={4,3,1};desc.layers=2;desc.samples=VK_SAMPLE_COUNT_4_BIT;desc.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto multi=c.create_image(desc),copy=c.create_image(desc);VkClearColorValue color{};color.float32[0]=color.float32[2]=color.float32[3]=1;
        c.clear(multi,multi->full_range(),color);c.copy(multi,copy,image_copy_region(multi,2));desc.samples=VK_SAMPLE_COUNT_1_BIT;auto resolved=c.create_image(desc);
        VkImageResolve r{};r.srcSubresource=r.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};r.extent=desc.extent;c.resolve(copy,resolved,r);
        auto bytes=c.readback(d.queue,resolved,image_region(resolved,0,0,2));bool okay=bytes.size()==96;
        for(size_t i=0;i<bytes.size();++i)okay=okay && bytes[i]==(i%4==1?0:255);
        check(okay,"Multisample clear/copy/resolve produces expected array pixels");
        refuses([&]{c.readback(d.queue,multi,image_region(multi));},"Multisampled image requires resolve before buffer readback");
        refuses([&]{c.resolve(resolved,resolved,r);},"Single-sample resolve source refused");
    }
    {
        gpu::ResourceCommands c(d.resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_R32_UINT;desc.extent={4,2,1};desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        auto image=c.create_image(desc);VkClearColorValue color{};color.uint32[0]=19;c.clear(image,image->full_range(),color);c.discard();
        color.uint32[0]=23;c.clear(image,image->full_range(),color);auto submitted=c.submit(d.queue);submitted->wait();submitted.reset();
        color.uint32[0]=29;c.clear(image,image->full_range(),color);c.discard();auto bytes=c.readback(d.queue,image,image_region(image));
        std::vector<uint32_t> expected(8,23);check(bytes.size()==32 && !memcmp(bytes.data(),expected.data(),32),"Discard preserves last submitted image data and rolls back unsubmitted layout state");
        auto bad=desc;bad.mips=8;refuses([&]{c.create_image(bad);},"Excessive mip chain refused");
        bad=desc;bad.extent.depth=2;refuses([&]{c.create_image(bad);},"2D volume description refused");
        bad=desc;bad.extent.width=0;refuses([&]{c.create_image(bad);},"Zero image extent refused");
        bad=desc;bad.samples=VkSampleCountFlagBits(3);refuses([&]{c.create_image(bad);},"Invalid sample bits refused");
        bad=desc;bad.flags=VK_IMAGE_CREATE_SPARSE_BINDING_BIT;refuses([&]{c.create_image(bad);},"Sparse ownership needs separate adapter");
        bad=desc;bad.format=VK_FORMAT_UNDEFINED;refuses([&]{c.create_image(bad);},"Unknown image format refused");
        gpu::ResourceBudgets tiny;tiny.resident=std::make_shared<assets::AllocationBudget>(1);gpu::ResourceCommands denied(d.resource,tiny);
        refuses([&]{denied.create_image(desc);},"Image resident budget enforced before oversized allocation");
        check(!tiny.resident->used(),"Failed image allocation does not retain budget");
        tiny.staging=std::make_shared<assets::AllocationBudget>(1);tiny.resident=budgets.resident;gpu::ResourceCommands no_stage(d.resource,tiny);
        auto staged=no_stage.create_image(desc);const uint32_t data[8]={};refuses([&]{no_stage.write(staged,image_region(staged),data,sizeof(data));},"Image staging budget enforced before CPU copy");
        check(!tiny.staging->used(),"Refused image upload releases staging budget");
        auto missing=d.resource;missing.GetImageFormatProperties=nullptr;gpu::ResourceCommands no_caps(missing,budgets);
        refuses([&]{no_caps.create_image(desc);},"No inferred image support without physical-device capabilities");
        missing=d.resource;missing.enabled_features.textureCompressionBC=VK_FALSE;gpu::ResourceCommands no_bc(missing,budgets);
        bad=desc;bad.format=VK_FORMAT_BC1_RGBA_UNORM_BLOCK;refuses([&]{no_bc.create_image(bad);},"Supported but disabled compression feature refused");
    }
    {
        auto resource=d.resource;real_image_submit=resource.dispatch.QueueSubmit;resource.dispatch.QueueSubmit=image_submit;
        auto c=std::make_unique<gpu::ResourceCommands>(resource,budgets);gpu::ImageDescription desc;desc.format=VK_FORMAT_R32_UINT;
        desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;auto image=c->create_image(desc);VkClearColorValue color{};color.uint32[0]=37;
        c->clear(image,image->full_range(),color);fail_image_submit=true;refuses([&]{c->submit(d.queue);},"Failed queue submission does not publish image layout");
        c->clear(image,image->full_range(),color);auto bytes=c->readback(d.queue,image,image_region(image));
        const uint32_t expected=37;check(bytes.size()==4 && !memcmp(bytes.data(),&expected,4),"Image can initialize after a rejected submission without stale layout");
        auto view=c->view(image,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_UNDEFINED,image->full_range());
        std::weak_ptr<gpu::ImageResource> weak=image;c->clear(image,image->full_range(),color);auto ticket=c->submit(d.queue);
        image.reset();c.reset();view.reset();check(!weak.expired(),"Submission owns images after views and executor retire");
        ticket->wait();ticket.reset();check(weak.expired(),"Completed image submission releases final image owner");
    }
    check(!budgets.resident->used() && !budgets.staging->used(),"Image/array/volume/view tests release all shared allocation ledgers");
    check(errors==0 && warnings==0,"Mutable image operations have clean core and synchronization validation");
}
