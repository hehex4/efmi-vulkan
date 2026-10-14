#include "graphics_link.h"
#include <spirv_reflect.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void reflected(SpvReflectResult result){require(result==SPV_REFLECT_RESULT_SUCCESS,"Graphics interface reflection/relocation failed");}
unsigned order(VkShaderStageFlagBits stage){
    switch(stage){case VK_SHADER_STAGE_VERTEX_BIT:return 0;case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:return 1;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:return 2;case VK_SHADER_STAGE_GEOMETRY_BIT:return 3;
    case VK_SHADER_STAGE_FRAGMENT_BIT:return 4;default:throw std::runtime_error("Non-graphics stage in graphics link");}
}
std::string semantic(const std::string& source){
    require(!source.empty() && source.size()<=1024,"Graphics input/output lacks a bounded original semantic");
    auto name=source;for(auto& c:name){require(static_cast<unsigned char>(c)<128,"Non-ASCII source semantic needs an adapter");if(c>='a' && c<='z')c-=32;}
    // HLSL's omitted semantic index is zero. Leading zeroes name the same index.
    auto start=name.find_last_not_of("0123456789");require(start!=std::string::npos,"Graphics semantic has no name");++start;
    auto digits=name.substr(start);name.resize(start);const auto nonzero=digits.find_first_not_of('0');
    digits=nonzero==std::string::npos?"0":digits.substr(nonzero);
    require(digits.size()<=10 && std::stoull(digits)<=UINT32_MAX,"Graphics semantic index is outside source range");
    return name+":"+digits;
}
struct Module {
    SpvReflectShaderModule value{};
    explicit Module(const HlslProgram& p){
        require(p.spirv.size()>=5 && p.spirv.size()<=16*1024*1024 && p.spirv[0]==SpvMagicNumber && p.spirv[3]<=1024*1024,
            "Graphics executable exceeds bounded module limits");
        reflected(spvReflectCreateShaderModule(p.spirv.size()*4,p.spirv.data(),&value));
    }
    ~Module(){spvReflectDestroyShaderModule(&value);}
    Module(const Module&)=delete;
};
void native_signature(const std::vector<ShaderSignature>& outputs,const std::vector<ShaderSignature>& inputs){
    require(outputs.size()<=4096 && inputs.size()<=4096,"Original signature exceeds graphics link budget");
    std::map<std::string,const ShaderSignature*> available;
    for(const auto& value:outputs)if(!value.stream)require(available.emplace(semantic(value.semantic+std::to_string(value.index)),&value).second,
        "Original shader output signature is ambiguous");
    for(const auto& input:inputs){const auto read=input.mask&input.read_write_mask;
        if(input.system_value || !read)continue; // Pipeline-generated and unused inputs.
        const auto found=available.find(semantic(input.semantic+std::to_string(input.index)));
        require(found!=available.end(),"Original DXBC input signature has no producer");const auto& output=*found->second;
        require(input.register_index==output.register_index && !(read&~output.mask) && input.component_type==output.component_type,
            "Original DXBC register/component signatures are incompatible");
    }
}
struct Shape {
    uint32_t scalar=0,width=0,components=0,columns=0,rows=0;
    std::vector<uint32_t> dimensions;
    bool patch=false;
    bool exact(const Shape& b) const {return std::tie(scalar,width,components,columns,rows,dimensions,patch)==
        std::tie(b.scalar,b.width,b.components,b.columns,b.rows,b.dimensions,b.patch);}
    uint32_t locations() const {
        uint64_t n=(uint64_t(width)*components+127)/128;if(columns)n*=columns;
        for(auto d:dimensions){require(d && n<=4096/d,"Graphics interface array location budget exceeded");n*=d;}
        require(n && n<=4096,"Graphics interface location budget exceeded");return uint32_t(n);
    }
};
Shape shape(const SpvReflectInterfaceVariable& v,VkShaderStageFlagBits stage,bool input){
    require(!v.member_count && v.type_description && (v.component==0 || v.component==UINT32_MAX),"Structured/packed graphics IO needs an explicit adapter");
    Shape s;const auto flags=v.type_description->type_flags;
    require(!(flags&SPV_REFLECT_TYPE_FLAG_BOOL),"Boolean graphics IO requires source lowering");
    s.scalar=(flags&SPV_REFLECT_TYPE_FLAG_FLOAT)?1:(flags&SPV_REFLECT_TYPE_FLAG_INT)?(v.numeric.scalar.signedness?3:2):0;
    s.width=v.numeric.scalar.width;s.components=std::max(1u,v.numeric.vector.component_count);
    s.columns=v.numeric.matrix.column_count;s.rows=v.numeric.matrix.row_count;s.patch=bool(v.decoration_flags&SPV_REFLECT_DECORATION_PATCH);
    require(s.scalar && (s.width==16 || s.width==32 || s.width==64) && s.components<=4 &&
        s.columns<=4 && s.rows<=4 && bool(s.columns)==bool(s.rows),"Graphics IO numeric shape is unsupported");
    require(v.array.dims_count<=SPV_REFLECT_MAX_ARRAY_DIMS,"Graphics IO array rank exceeds reflection bound");
    const bool vertices=!s.patch && ((input && (stage==VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
        stage==VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT || stage==VK_SHADER_STAGE_GEOMETRY_BIT)) ||
        (!input && stage==VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT));
    require(!vertices || v.array.dims_count,"Per-vertex graphics IO is missing its outer array");
    for(uint32_t i=0;i<v.array.dims_count;++i){require(v.array.dims[i] && v.array.spec_constant_op_ids[i]==UINT32_MAX,
        "Runtime/specialized graphics IO array needs an adapter");if(!vertices || i)s.dimensions.push_back(v.array.dims[i]);}
    s.locations();return s;
}
struct Variable {
    SpvReflectInterfaceVariable* reflected=nullptr;
    ShaderInterface* source=nullptr;
    Shape type;
};
using Variables=std::map<std::pair<bool,std::string>,Variable>;
Variables variables(HlslProgram& p,Module& module,bool input){
    auto& records=input?p.inputs:p.outputs;const auto count=input?module.value.input_variable_count:module.value.output_variable_count;
    auto** values=input?module.value.input_variables:module.value.output_variables;
    require(records.size()<=1024 && count<=1024,"Graphics IO count exceeds the bounded linker");
    std::map<uint32_t,ShaderInterface*> ids;
    for(auto& record:records)if(record.builtin==UINT32_MAX)require(record.spirv_id && ids.emplace(record.spirv_id,&record).second,
        "Graphics original semantic identity is missing or duplicated");
    Variables result;std::set<std::pair<bool,uint32_t>> locations;
    for(uint32_t i=0;i<count;++i){auto* value=values[i];if(value->built_in>=0)continue;
        require(!value->member_count,"User interface blocks need explicit member relocation");
        const auto found=ids.find(value->spirv_id);require(found!=ids.end(),"Executable graphics variable has no original semantic identity");
        auto* source=found->second;require(source->location==value->location && source->format==VkFormat(value->format),"Original graphics interface metadata differs from executable");
        const auto type=shape(*value,p.stage,input);require(value->location<=4096-type.locations(),"Graphics variable location is outside linker budget");
        for(uint32_t n=0;n<type.locations();++n)require(locations.emplace(type.patch,value->location+n).second,"Graphics interface locations overlap");
        require(result.emplace(std::make_pair(type.patch,semantic(source->semantic)),Variable{value,source,type}).second,"Original graphics semantic is ambiguous");ids.erase(found);
    }
    require(ids.empty(),"Original graphics semantic metadata has no executable variable");return result;
}
bool compatible(const Shape& output,const Shape& input,bool maintenance4){
    if(output.exact(input))return true;
    return maintenance4 && output.patch==input.patch && output.dimensions.empty() && input.dimensions.empty() &&
        !output.columns && !input.columns && output.scalar==input.scalar && output.width==input.width &&
        output.components>input.components && input.components>1;
}
}
void validate_geometry(const HlslProgram& program,VkPrimitiveTopology topology,const VkPhysicalDeviceLimits& limits){
    require(program.stage==VK_SHADER_STAGE_GEOMETRY_BIT,"Geometry validation requires a geometry program");
    Module module(program);require(module.value.entry_point_count==1,"Geometry requires one entry point");
    const auto& entry=module.value.entry_points[0];bool matching=false,triangles=false;
    for(uint32_t i=0;i<entry.execution_mode_count;++i)switch(entry.execution_modes[i]){
        case SpvExecutionModeInputPoints:matching=topology==VK_PRIMITIVE_TOPOLOGY_POINT_LIST;break;
        case SpvExecutionModeInputLines:matching=topology==VK_PRIMITIVE_TOPOLOGY_LINE_LIST || topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;break;
        case SpvExecutionModeTriangles:matching=topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;break;
        case SpvExecutionModeOutputTriangleStrip:triangles=true;break;
        default:break;
    }
    require(matching,"Geometry input primitive differs from supported assembly topology");
    require(triangles,"Geometry point/line output requires another raster adapter");
    require(entry.invocations && entry.invocations<=limits.maxGeometryShaderInvocations,"Geometry invocation count exceeds device limit");
    require(entry.output_vertices && entry.output_vertices<=limits.maxGeometryOutputVertices,"Geometry output vertex count exceeds device limit");
    auto components=[&](bool input){uint64_t count=0;const auto n=input?entry.input_variable_count:entry.output_variable_count;
        auto** variables=input?entry.input_variables:entry.output_variables;require(n<=1024,"Geometry interface count exceeds budget");
        for(uint32_t i=0;i<n;++i){const auto& v=*variables[i];const bool builtin=v.built_in>=0;
            // Invocation/primitive IDs are scalar system inputs, not per-vertex arrays.
            const bool vertex_input=input && (!builtin || v.built_in==SpvBuiltInPosition || v.built_in==SpvBuiltInPointSize ||
                v.built_in==SpvBuiltInClipDistance || v.built_in==SpvBuiltInCullDistance);
            const auto s=shape(v,program.stage,vertex_input);uint64_t size=uint64_t((s.width+31)/32)*s.components*std::max(1u,s.columns);
            for(auto dim:s.dimensions)size*=dim;count+=size;
            const uint32_t limit=input?limits.maxGeometryInputComponents:limits.maxGeometryOutputComponents;
            require(count<=limit,"Geometry interface components exceed device limit");
            if(!builtin)require(uint64_t(v.location)*4+uint64_t(s.locations()-1)*4+uint64_t((s.width+31)/32)*s.components<=limit,
                "Geometry interface location exceeds device component limit");
        }return count;};
    components(true);const auto outputs=components(false);
    require(outputs*entry.output_vertices<=limits.maxGeometryTotalOutputComponents,"Geometry total output components exceed device limit");
}
std::vector<HlslProgram> link_graphics(const std::vector<HlslProgram>& programs,bool maintenance4){
    require(!programs.empty() && programs.size()<=5,"Graphics link requires one to five stages");
    // Bound executable payload before copying source programs for relocation.
    uint64_t payload=0;for(const auto& p:programs){require(p.spirv.size()<=16*1024*1024 && p.dxbc.size()<=64*1024*1024 &&
        p.dependencies.size()<=1024 && p.inputs.size()<=1024 && p.outputs.size()<=1024 && p.input_signature.size()<=4096 &&
        p.output_signature.size()<=4096 && p.patch_signature.size()<=4096,"Graphics source exceeds bounded linker input");
        payload+=p.spirv.size()*4+p.dxbc.size();for(const auto& dependency:p.dependencies){require(dependency.bytes.size()<=64*1024*1024,"Graphics dependency exceeds source budget");payload+=dependency.bytes.size();}
        require(payload<=256ull*1024*1024,"Graphics link source payload budget exceeded");}
    auto result=programs;std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return order(a.stage)<order(b.stage);});
    require(result.front().stage==VK_SHADER_STAGE_VERTEX_BIT,"Graphics link requires a vertex stage");
    std::set<unsigned> seen;for(const auto& p:result)require(seen.insert(order(p.stage)).second,"Duplicate graphics stage");
    require(bool(seen.count(1))==bool(seen.count(2)),"Hull/domain stages must form a complete pair");
    std::vector<std::unique_ptr<Module>> modules;std::vector<Variables> inputs,outputs;
    for(auto& p:result){auto module=std::make_unique<Module>(p);
        require(module->value.entry_point_count==1 && module->value.shader_stage==uint32_t(p.stage),"Graphics executable entry point/stage differs");
        inputs.push_back(variables(p,*module,true));outputs.push_back(variables(p,*module,false));modules.push_back(std::move(module));}
    for(size_t i=1;i<result.size();++i){std::set<std::pair<bool,uint32_t>> occupied;
        // D3D11 does not relocate adjacent native registers on behalf of the
        // application. Semantic mapping must not repair an invalid DXBC pair.
        native_signature(result[i-1].output_signature,result[i].input_signature);
        if(result[i].stage==VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            native_signature(result[i-1].patch_signature,result[i].patch_signature);
        for(auto& pair:inputs[i]){auto& input=pair.second;const auto match=outputs[i-1].find(pair.first);
            if(match==outputs[i-1].end())throw std::runtime_error("Graphics input has no producer for semantic "+pair.first.second);
            const auto& output=match->second;
            require(compatible(output.type,input.type,maintenance4),"Graphics semantic types differ; enabled maintenance4 or shape lowering is required");
            const auto location=output.reflected->location;
            for(uint32_t n=0;n<input.type.locations();++n)require(occupied.emplace(input.type.patch,location+n).second,"Linked graphics locations overlap");
            reflected(spvReflectChangeInputVariableLocation(&modules[i]->value,input.reflected,location));input.source->location=location;
        }
    }
    for(size_t i=0;i<result.size();++i){const auto* words=spvReflectGetCode(&modules[i]->value);const auto bytes=spvReflectGetCodeSize(&modules[i]->value);
        require(words && bytes%4==0,"Linked graphics executable is missing");result[i].spirv.assign(words,words+bytes/4);}
    return result;
}
}
