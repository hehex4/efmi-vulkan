// Reuse the existing headless loader/validation fixture in a separate executable.
// Generated cases live under build/ and contain only verified capture metadata.
#define wmain unused_asset_main
#include "asset_gpu.cpp"
#undef wmain
#include "../layer/src/json.h"

struct HistoricalCase {
    efvk::GraphicsRecipe recipe;
    efvk::PipelineLayoutInfo layout;
    efvk::VertexInputRecipe vertex;
    efvk::FragmentPatch fragment;
    std::string expected_recipe,expected_layout,expected_vertex,expected_patch,expected_source_vertex,expected_dynamic;
};
template<class T>static void field(T& target,int64_t value){target=static_cast<T>(value);}
static float float_bits(uint32_t value){float f;memcpy(&f,&value,4);return f;}
static std::shared_ptr<efvk::ShaderInfo> shader_file(const char* path,uint32_t id){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(!file || size<20 || size>(16<<20) || size%4)throw std::runtime_error("Invalid archived shader");
    auto shader=std::make_shared<efvk::ShaderInfo>();shader->id=id;shader->size=static_cast<uint32_t>(size);shader->code.resize(size_t(size)/4);
    file.seekg(0);file.read(reinterpret_cast<char*>(shader->code.data()),size);
    if(!file)throw std::runtime_error("Archived shader read failed");return shader;
}
static std::string vertex_json(const efvk::VertexInputRecipe& v){
    efvk::JsonWriter w;w.begin_object();w.key("bindings");w.begin_array();
    for(const auto& b:v.bindings){w.begin_object();w.kn("binding",b.binding);w.kn("stride",b.stride);w.kb("per_instance",b.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE);w.end_object();}
    w.end_array();w.key("attributes");w.begin_array();
    for(const auto& a:v.attributes){w.begin_object();w.kn("location",a.location);w.kn("binding",a.binding);w.kn("format",a.format);w.kn("offset",a.offset);w.end_object();}
    w.end_array();w.end_object();return w.out;
}
static std::string layout_json(const efvk::PipelineLayoutInfo& layout){
    efvk::JsonWriter w;w.begin_object();w.kb("complete",layout.complete);w.kn("flags",layout.flags);w.key("sets");w.begin_array();
    for(const auto& s:layout.sets){w.begin_object();w.kn("id",s->id);w.kb("complete",s->complete);w.kn("flags",s->flags);w.key("bindings");w.begin_array();
        for(const auto& item:s->bindings){const auto& b=item.second;w.begin_object();w.kn("binding",item.first);w.kn("type",b.type);w.kn("count",b.count);
            w.kn("stages",b.stages);w.kn("flags",b.flags);w.kn("immutable_samplers",b.immutable_samplers.size());w.end_object();}
        w.end_array();w.end_object();}
    w.end_array();w.end_object();return w.out;
}
static std::string patch_json(const efvk::FragmentPatch& patch){
    efvk::JsonWriter w;w.begin_object();w.key("images");w.begin_array();
    for(const auto& i:patch.images){w.begin_array();w.num(i.set);w.num(i.binding);w.end_array();}
    w.end_array();w.key("nulls");w.begin_array();for(const auto& i:patch.nulls){w.begin_array();w.num(i.set);w.num(i.binding);w.end_array();}
    w.end_array();w.end_object();return w.out;
}
#include "cases.inc"

int wmain(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
    unsigned completed=0;
    try{Gpu g;g.init(true);efvk::VariantDispatch dispatch;
#define X(n) dispatch.n=p##n;
        EFVK_VARIANT_FUNCS(X)
#undef X
        VkPhysicalDeviceProperties properties{};pGetPhysicalDeviceProperties(g.physical,&properties);
        efvk::JsonWriter device;device.begin_object();device.kv("device",properties.deviceName);device.kn("vendor",properties.vendorID);
        device.kn("driver",properties.driverVersion);device.kn("api",properties.apiVersion);device.end_object();printf("%s\n",device.out.c_str());
        for(unsigned i=0;i<historical_case_count;++i){
            auto c=historical_case(i);efvk::JsonWriter recipe;c.recipe.write(recipe);
            // Exact roundtrip is checked before asking the driver to create anything.
            efvk::JsonWriter dynamic;dynamic.begin_array();for(auto value:c.recipe.dynamic)dynamic.num(value);dynamic.end_array();
            const bool equal=recipe.out==c.expected_recipe && layout_json(c.layout)==c.expected_layout &&
                vertex_json(c.vertex)==c.expected_vertex && patch_json(c.fragment)==c.expected_patch &&
                vertex_json(c.recipe.vertex)==c.expected_source_vertex && dynamic.out==c.expected_dynamic &&
                c.recipe.blend.attachmentCount==c.recipe.blend_attachments.size();
            check(equal,"archived recipe/layout/vertex/material roundtrip");
            if(!equal)throw std::runtime_error("Historical metadata roundtrip differs at case "+std::to_string(i));
            efvk::PipelineVariant variant(g.device,dispatch);
            const bool ok=variant.build(c.recipe,c.layout,c.vertex,properties.limits.maxBoundDescriptorSets,c.fragment,properties.limits);
            efvk::JsonWriter result;result.begin_object();result.kn("case",i);result.kb("created",ok);result.kv("reason",variant.reason);
            result.ki("result",variant.result);result.end_object();printf("%s\n",result.out.c_str());
            check(ok && variant.pipeline && variant.layout,"archived whole-character variant creates on the driver");
            if(ok)++completed;
        }
    }catch(const std::exception& error){++failed;printf("FAIL: %s\n",error.what());}
    check(!validation_errors && !validation_warnings,"historical pipeline validation remains clean");
    printf("historical_variants: %u created, %d checks passed, %d failed; validation errors=%u warnings=%u\n",
           completed,passed,failed,validation_errors.load(),validation_warnings.load());return failed?1:0;
}
