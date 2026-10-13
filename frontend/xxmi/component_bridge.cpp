#include "component_bridge.h"
#include "spatial_palettes.h"
#include "buffer_queries.h"
#include "globals.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace efmi::frontend {
namespace {
const std::wstring ns=L"\\efmiv1\\";
std::wstring command(const wchar_t* name){return L"commandlist"+ns+name;}
CommandListVariable& variable(const wchar_t* name){return command_list_globals.at(L"$"+ns+name);}
CustomResource& resource(const wchar_t* name){return customResources.at(L"resource"+ns+name);}
CustomResourcePool& pool(const wchar_t* name){return customResourcePools.at(L"pool"+ns+name);}
ExplicitCommandListSection& list(const wchar_t* name){return explicitCommandListSections.at(command(name));}
bool number(float value,uint32_t& result)
{
    if(!std::isfinite(value) || value<0 || value>65536 || std::floor(value)!=value)return false;
    result=uint32_t(value);return true;
}
bool same(const SkeletonFilePlan& a,const SkeletonFilePlan& b)
{
    if(a.bones!=b.bones || a.instances!=b.instances || a.lod_count!=b.lod_count || a.components.size()!=b.components.size())return false;
    for(size_t i=0;i<a.components.size();++i){
        if(a.components[i].first!=b.components[i].first || a.components[i].count!=b.components[i].count)return false;
        for(size_t lod=0;lod<2;++lod)
            if(a.components[i].lods[lod].source!=b.components[i].lods[lod].source ||
               a.components[i].lods[lod].path!=b.components[i].lods[lod].path)return false;
    }
    return true;
}
}
struct CoreComponentBridge::Impl {
    DrawPlanAdapter& adapter;const FilePlanCompiler& files;
    SpatialPalettes palettes;BufferQueries queries;SkeletonFilePlan skeleton;
    CustomResourcePool* spatial=nullptr;
    CustomResource* skeleton_resource=nullptr;
    CustomResource* initialization_target=nullptr;
    unsigned initialization_requests=0;
    bool preparing=false,ready=false,active=false,observed=false,attached=false,resolve_textures=false;
    ComponentInput input;std::vector<uint32_t> hashes;
    std::shared_ptr<const core::PaletteSnapshot> selected;
    uint64_t current_identity=0;
    Impl(DrawPlanAdapter& a,const FilePlanCompiler& f):adapter(a),files(f){}
    bool fail(std::wstring& why,const wchar_t* text){why=text;return false;}
    bool initialize(std::wstring& why)
    {
        auto* target=adapter.referenced_resource(&resource(L"outputmergedskeleton_template"));
        if(!adapter.has_reference(&resource(L"outputmergedskeleton_template")) || !target)
            return fail(why,L"Native skeleton initialization has no declared output");
        if(preparing){initialization_target=target;++initialization_requests;return true;}
        // Preparation already owns the immutable implicit matrices. A runtime
        // call is legal only for that exact declared skeleton and layout.
        if(!active || !ready || target!=skeleton_resource)return fail(why,L"Runtime initialization targets an unprepared skeleton");
        return true;
    }
    bool read_config(CommandListState& state,std::wstring& why)
    {
        if(!active || !ready || !state.call_info || state.call_info->InstanceCount!=1 || state.call_info->FirstInstance!=0)
            return fail(why,L"Native Core requires one resolved original rolling-buffer instance");
        variable(L"instance_config_vs_cb_slot").fval=float(input.vs_cb_slot);
        variable(L"instance_config_ps_cb_slot").fval=float(input.ps_cb_slot);
        variable(L"instance_config_vs_cb_byte_offset").fval=0;
        variable(L"instance_config_ps_cb_byte_offset").fval=0;
        return true;
    }
    bool attach(std::wstring& why)
    {
        uint32_t component=0;
        if(!active || !number(variable(L"component_id").fval,component) || component>=skeleton.components.size() ||
           !skeleton.components[component].count || adapter.referenced_resource(&resource(L"output_mergedskeleton"))!=skeleton_resource)
            return fail(why,L"Native Core component has no prepared merged skeleton");
        SkeletonFilePlan current;
        if(!read_skeleton_plan(skeleton.bones,skeleton.instances,skeleton.lod_count,
              pool(L"input_mergedskeleton_component_vertexgroupoffsets"),pool(L"input_mergedskeleton_component_vertexgroupcounts"),
              pool(L"input_mergedskeleton_component_lodremaps"),adapter,files,current,why))return false;
        if(!same(skeleton,current))return fail(why,L"Runtime skeleton metadata differs from the prepared generation");
        variable(L"component_vg_offset").fval=float(current.components[component].first);
        variable(L"component_vg_count").fval=float(current.components[component].count);
        variable(L"bones_data_source").fval=1;attached=true;return true;
    }
    bool identified(std::wstring& why)
    {
        uint32_t component=0,lod=0,bones=0,instances=0,components=0,status=0,slot=0;
        if(!active || !ready || hashes.size()!=1 ||
           pool(L"input_objectspatialidentity").ResolvePool()!=spatial ||
           !number(variable(L"component_id").fval,component) || component>=skeleton.components.size() || !skeleton.components[component].count ||
           !number(variable(L"lod_level").fval,lod) || lod>=2 ||
           !number(variable(L"bones_count").fval,bones) || bones!=skeleton.bones ||
           !number(variable(L"instance_count").fval,instances) || instances!=skeleton.instances ||
           !number(variable(L"component_count").fval,components) || components!=skeleton.components.size() ||
           variable(L"gpu_posed").fval!=1 || variable(L"skip_skeleton_override").fval!=0 ||
           !number(pool(L"spatialidentity_identifiedinstances").GetVariable(0,false,true,false)->fval,status) || status<1 || status>3 ||
           !number(pool(L"spatialidentity_spatialids").GetVariable(0,false,true,false)->fval,slot))
            return fail(why,L"Native Core input, identity or skeleton contract is unsupported");
        std::vector<uint64_t> identities;std::string error;
        if(!palettes.observe(input.frame,input.frame_seconds,{{slot,hashes[0]}},identities,error)){
            why.assign(error.begin(),error.end());return false;
        }
        observed=true;current_identity=identities[0];
        const auto result=palettes.import_component(current_identity,component,lod,input.entry,input.current,input.previous,
                                                    variable(L"custom_mesh_scale").fval,error);
        if(result==efvk::BoneImportResult::Rejected){why.assign(error.begin(),error.end());return false;}
        // Prepared metadata permits collecting providers during the original
        // first-frame delay without prematurely running mod connection callbacks.
        if(status==2)return fail(why,L"Original Core selected the original instance");
        return true;
    }
    bool detect(std::wstring& why)
    {
        if(!active || !ready || !observed)return fail(why,L"Bone-source detection requires a resolved original instance");
        variable(L"bones_data_source").fval=1;return true;
    }
    bool apply(std::wstring& why)
    {
        if(!active || !ready || !observed || !attached || !current_identity ||
           variable(L"draw_call_instance_id").fval!=0 || variable(L"bones_data_source").fval!=1 ||
           variable(L"skip_skeleton_override").fval!=0 ||
           adapter.referenced_resource(&resource(L"output_mergedskeleton"))!=skeleton_resource)
            return fail(why,L"Native skeleton apply requires the attached single rolling-buffer instance");
        std::string error;
        if(!palettes.snapshot(current_identity,input.entry,selected,error)){why.assign(error.begin(),error.end());return false;}
        variable(L"merged_skeleton_instance_id").fval=0;
        variable(L"merged_skeleton_offset").fval=1536;
        return true;
    }
};
CoreComponentBridge::CoreComponentBridge(DrawPlanAdapter& adapter,const FilePlanCompiler& files)
    :impl_(std::make_shared<Impl>(adapter,files)){}
std::vector<std::wstring> CoreComponentBridge::boundaries() const
{
    std::vector<std::wstring> result={command(L"initializemergedskeleton"),command(L"component_readconfig"),
            command(L"mergedskeleton_attachcomponent"),command(L"mergedskeleton_detectbonedatasource"),
            command(L"mergedskeleton_apply")};
    if(!impl_->resolve_textures)result.push_back(command(L"overridetextures"));return result;
}
void CoreComponentBridge::install()
{
    auto owner=impl_;auto& adapter=owner->adapter;
    owner->resolve_textures=adapter.has_texture_queries();
    adapter.native(list(L"initializemergedskeleton"),command(L"initializemergedskeleton"),
        [owner](CommandListState&,unsigned phases,std::wstring& why){return !(phases&1) || owner->initialize(why);});
    adapter.native(list(L"component_readconfig"),command(L"component_readconfig"),
        [owner](CommandListState& state,unsigned phases,std::wstring& why){return !(phases&1) || owner->read_config(state,why);});
    adapter.native(list(L"mergedskeleton_attachcomponent"),command(L"mergedskeleton_attachcomponent"),
        [owner](CommandListState&,unsigned phases,std::wstring& why){return !(phases&1) || owner->attach(why);});
    adapter.native(list(L"mergedskeleton_detectbonedatasource"),command(L"mergedskeleton_detectbonedatasource"),
        [owner](CommandListState&,unsigned phases,std::wstring& why){return !(phases&1) || owner->detect(why);});
    adapter.native(list(L"mergedskeleton_apply"),command(L"mergedskeleton_apply"),
        [owner](CommandListState&,unsigned phases,std::wstring& why){return !(phases&1) || owner->apply(why);});
    adapter.after(list(L"spatialidentity_identifycomponentinstances"),
        [owner](CommandListState&,unsigned phases,std::wstring& why){return !(phases&1) || owner->identified(why);});
    if(!owner->resolve_textures)adapter.external(list(L"overridetextures"),command(L"overridetextures"));
    adapter.resource_queries([owner](ResourceCopyTarget& target,CommandListState& state,float& value,std::wstring& why){
        if(!owner->active)return owner->fail(why,L"Core resource query outside active planning");
        if(target.type==ResourceCopyTargetType::CUSTOM_RESOURCE && !target.custom_resource_pool &&
           target.evaluation_mode==ResourceCopyTargetEvaluationMode::RESOURCE_IDENTITY &&
           target.GetCustomResource(&state)==&resource(L"output_mergedskeleton")){
            // A stable native identity for this one prepared skeleton, or zero
            // for the original Core's explicit null reference. No D3D pointer.
            auto* target_resource=owner->adapter.referenced_resource(&resource(L"output_mergedskeleton"));
            if(!owner->adapter.has_reference(&resource(L"output_mergedskeleton")))target_resource=nullptr;
            if(target_resource && target_resource!=owner->skeleton_resource)return owner->fail(why,L"Unprepared Core output identity");
            value=target_resource?1.f:0.f;return true;
        }
        if(pool(L"input_objectspatialidentity").ResolvePool()!=owner->spatial)
            return owner->fail(why,L"Original spatial query uses an unprepared pool");
        if(!owner->queries.read(target,state,value,why))return false;
        uint32_t hash;std::memcpy(&hash,&value,4);owner->hashes.push_back(hash);owner->observed=false;return true;
    });
    adapter.palette_source([owner](CommandListState&,std::shared_ptr<const core::PaletteSnapshot>& output,std::wstring& why){
        if(!owner->active || !owner->selected)return owner->fail(why,L"Custom draw has no complete native palette");
        output=owner->selected;return true;
    });
}
bool CoreComponentBridge::prepare(const SkeletonSetup& setup,CommandListState& state,std::wstring& why)
{
    auto& p=*impl_;why.clear();uint32_t bones=0,instances=0,lods=0;
    if(p.active || p.preparing || !setup.initialize || !setup.bones || !setup.instances || !setup.offsets ||
       !setup.counts || !setup.remaps || !setup.spatial || !number(setup.bones->fval,bones) ||
       !number(setup.instances->fval,instances) || !number(variable(L"cfg_ms_max_lod_level_count").fval,lods) ||
       variable(L"cfg_ms_implicit_bones_count").fval!=512 || variable(L"cfg_ms_skeletons_count").fval!=2 ||
       variable(L"cfg_ms_bone_entry_size").fval!=3)return p.fail(why,L"Core preparation declarations are incompatible");
    p.ready=false;p.preparing=true;p.initialization_requests=0;p.initialization_target=nullptr;DrawPlan result;
    const bool executed=p.adapter.execute_pair(setup.initialize->command_list,setup.initialize->post_command_list,state,result,why);p.preparing=false;
    if(!executed)return false;
    if(p.initialization_requests!=1 || !p.initialization_target || !result.items.empty() || result.skip_original)
        return p.fail(why,L"Core preparation requires one native initialization and evaluated metadata only");
    SkeletonFilePlan plan;
    if(!read_skeleton_plan(bones,instances,lods,*setup.offsets,*setup.counts,*setup.remaps,p.adapter,p.files,plan,why))return false;
    core::PaletteDefinition definition;std::string error;
    if(!load_palette_files(plan,definition,error) || !p.palettes.configure(*setup.spatial,std::move(definition),error)){
        why.assign(error.begin(),error.end());return false;
    }
    p.skeleton=std::move(plan);p.spatial=setup.spatial->ResolvePool();p.skeleton_resource=p.initialization_target;p.ready=true;return true;
}
bool CoreComponentBridge::plan(CommandList& root,CommandList& post,CommandListState& state,const ComponentInput& input,DrawPlan& output,std::wstring& why)
{
    auto& p=*impl_;why.clear();
    // Guard the original pool's float-to-uint conversion before original code runs.
    const float radius=p.spatial?std::ceil(p.spatial->spatial_radius*(input.frame_seconds*120.0f)):0;
    if(!p.ready || p.active || !std::isfinite(input.frame_seconds) || input.frame_seconds<=0 ||
       !std::isfinite(radius) || radius<0 || double(radius)>UINT32_MAX ||
       input.vs_cb_slot<1 || input.vs_cb_slot>3 || input.ps_cb_slot<1 || input.ps_cb_slot>2 ||
       !state.call_info || state.call_info->InstanceCount!=1 || state.call_info->FirstInstance!=0)
        return p.fail(why,L"Core draw requires a supported, resolved single-instance profile");
    p.input=input;p.hashes.clear();p.selected.reset();p.current_identity=0;p.observed=false;p.attached=false;
    if(!p.queries.bind(&resource(L"original_vs_cb"),p.input.entry.data(),p.input.entry.size(),why))return false;
    G->frame_no=input.frame;G->gFrameTime=input.frame_seconds;
    p.active=true;const bool ok=p.adapter.execute_pair(root,post,state,output,why);p.active=false;
    if(!ok && !p.observed)p.palettes.invalidate();
    p.queries.clear();p.selected.reset();return ok;
}
}
