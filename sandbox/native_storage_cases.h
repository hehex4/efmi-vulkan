#include "../frontend/gpu/native_shader.h"

static std::vector<uint32_t> native_storage_shader(bool expand=false)
{
    spv::Builder b;
    const auto main=b.id(),source=b.id(),destination=b.id(),v=b.id(),fn=b.id(),u=b.id(),v2=b.id(),v4=b.id();
    const auto image=b.id(),pointer=b.id(),zero=b.id(),coord=b.id(),label=b.id(),read_image=b.id(),write_image=b.id(),pixel=b.id();
    b.op(spv::OpCapability,{1});b.op(spv::OpCapability,{55});b.op(spv::OpCapability,{56});
    b.op(spv::OpMemoryModel,{0,1});b.entry_point(5,main,"main",{});b.op(spv::OpExecutionMode,{main,17,1,1,1});
    b.op(spv::OpDecorate,{source,34,2});b.op(spv::OpDecorate,{source,33,3});
    b.op(spv::OpDecorate,{destination,34,4});b.op(spv::OpDecorate,{destination,33,3});
    b.op(spv::OpTypeVoid,{v});b.op(spv::OpTypeFunction,{fn,v});b.op(spv::OpTypeInt,{u,32,0});
    b.op(spv::OpTypeVector,{v2,u,2});b.op(spv::OpTypeVector,{v4,u,4});
    b.op(spv::OpTypeImage,{image,u,1,0,0,0,2,0});b.op(spv::OpTypePointer,{pointer,0,image});
    b.op(spv::OpConstant,{u,zero,0});b.op(spv::OpConstantComposite,{v2,coord,zero,zero});
    b.op(spv::OpVariable,{pointer,source,0});b.op(spv::OpVariable,{pointer,destination,0});
    b.op(spv::OpFunction,{v,main,0,fn});b.op(spv::OpLabel,{label});
    b.op(spv::OpLoad,{image,read_image,source});b.op(spv::OpLoad,{image,write_image,destination});
    b.op(spv::Op(98),{v4,pixel,read_image,coord});b.op(spv::Op(99),{write_image,coord,pixel});
    if(expand)b.op(spv::Op(99),{read_image,coord,pixel});
    b.op(spv::OpReturn,{});b.op(spv::OpFunctionEnd,{});return b.finish();
}

static void native_storage_access_cases()
{
    const auto code=native_storage_shader();std::vector<efmi::gpu::NativeStorageUse> uses;std::string why;
    check(efmi::gpu::native_shader_storage_usage(code,uses,why) && uses.size()==2,
        "native storage provenance supports arbitrary sets with repeated binding numbers");
    bool source=false,destination=false;
    for(const auto& use:uses){
        source|=use.set==2 && use.binding==3 && use.read && !use.write && !use.atomic;
        destination|=use.set==4 && use.binding==3 && !use.read && use.write && !use.atomic;
    }
    check(source && destination,"native formatless access distinguishes reads from writes per descriptor");
    check(efmi::gpu::native_shader_interface(code,code,VK_SHADER_STAGE_COMPUTE_BIT,"main",why),
        "identical formatless shader preserves its access contract");
    check(!efmi::gpu::native_shader_interface(code,native_storage_shader(true),VK_SHADER_STAGE_COMPUTE_BIT,"main",why) &&
        why.find("storage access")!=std::string::npos,
        "ShaderFix cannot expand unannotated formatless writes behind the resource checks");
}

static VkFormatFeatureFlags2 native_test_format_features=0;
static unsigned native_test_format_queries=0;
static VKAPI_ATTR void VKAPI_CALL native_test_format_query(VkPhysicalDevice,VkFormat,VkFormatProperties2* properties)
{
    ++native_test_format_queries;
    static_cast<VkFormatProperties3*>(properties->pNext)->optimalTilingFeatures=native_test_format_features;
}
static void native_storage_format_cases()
{
    Device d;d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(701));
    d.api_version=d.app_api_version=VK_API_VERSION_1_3;d.format_properties2=native_test_format_query;
    d.enabled_features.shaderStorageImageReadWithoutFormat=VK_TRUE;
    const auto pipeline=h<VkPipeline>(702);d.pipes[pipeline].id=1;d.native_image_cache=std::make_shared<NativeImageCache>();
    auto access=std::make_shared<NativeImageCache::Access>();
    access->images.push_back({{0,3,1,0,false,false,false,false,2,0},false,true,false,false});
    d.native_image_cache->pipelines[1]=access;
    auto image=std::make_shared<efmi::frontend::NativeResource>();image->identity=6;
    image->image=703;image->image_view=704;image->keep=std::make_shared<int>(1);image->image_safe=true;
    image->view_type=VK_IMAGE_VIEW_TYPE_2D;image->view_format=VK_FORMAT_R32_UINT;image->usage=VK_IMAGE_USAGE_STORAGE_BIT;
    image->levels=image->layers=image->mip_count=image->layer_count=1;image->aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    CmdBuf cb;cb.image_layouts.set(1,1,1,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_LAYOUT_GENERAL);
    DrawRec draw;draw.kind=DK_DISPATCH;draw.pipeline=pipeline;
    efmi::frontend::NativeBinding target;target.binding=3;target.kind=efmi::frontend::NativeBinding::Access;
    NativeImagePlan plan;VkImageLayout layout;std::string why;
    auto accepted=[&](){why.clear();return native_image_binding(&d,&cb,draw,target,image,plan,layout,why);};
    native_test_format_features=VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;native_test_format_queries=0;
    check(accepted() && accepted() && native_test_format_queries==1,
        "formatless reads need no write feature and reuse cached format capabilities");
    access->images[0].write=true;
    check(!accepted() && why.find("not enabled")!=std::string::npos,
        "formatless writes require the enabled device feature");
    d.enabled_features.shaderStorageImageWriteWithoutFormat=VK_TRUE;
    check(!accepted() && why.find("image format")!=std::string::npos,
        "device feature alone does not prove formatless write support for a view");
    native_test_format_features|=VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;d.native_image_cache->storage_formats.clear();
    check(accepted(),"formatless read/write works when both per-format capabilities are present");
    access->images[0].read=false;d.enabled_features.shaderStorageImageReadWithoutFormat=VK_FALSE;
    check(accepted(),"write-only formatless storage does not require the read feature");
    d.app_api_version=VK_API_VERSION_1_1;
    check(!accepted() && why.find("feature queries")!=std::string::npos,
        "old API without enabled format-flags extension cannot assume 64-bit query support");
    d.extensions.push_back(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
    check(accepted(),"enabled format-flags extension supplies the pre-1.3 query path");
    image->view_format=VK_FORMAT_R32_SFLOAT;
    check(!accepted() && why.find("numeric")!=std::string::npos,
        "formatless views still require the consumer numeric class");
    image->view_format=VK_FORMAT_R16_UINT;access->images[0].resource.image_format=33;
    check(!accepted() && why.find("declared shader format")!=std::string::npos,
        "typed storage requires exact declared format even within one numeric class");
}
