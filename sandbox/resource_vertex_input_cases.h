// Preserve native fetched register bits across float/int/uint input signatures.
#include <spirv_reflect.h>
static const float input_type_vertices[]={1,2,3,4,1,2,3,4,1,2,3,4};
static std::vector<gpu::HlslProgram> input_type_programs(const std::filesystem::path& root,const char* scalar,unsigned width){
    std::filesystem::create_directories(root/"Mods");std::string code=R"hlsl(struct O{float4 p:SV_Position;nointerpolation uint4 bits:COLOR;};
#ifdef VERTEX_SHADER
struct I{)hlsl";code+=std::string(scalar)+(width==1?"":std::to_string(width))+" a:ALPHA;uint id:SV_VertexID;};";
    code+="O main(I i){O o;o.p=float4(i.id==2?3:-1,i.id==1?3:-1,0,1);o.bits=";
    if(width==4)code+="asuint(i.a)";else {code+="uint4(asuint(i.a)";for(unsigned c=width;c<4;++c)code+=", "+std::to_string(c+17);code+=")";}
    code+=R"hlsl(;return o;}
#else
uint4 main(O o):SV_Target{return o.bits;}
#endif
)hlsl";write(root/"Mods/input.hlsl",code.c_str());return geometry_programs(root/"Mods/input.hlsl","vp");
}
static std::vector<uint8_t> native_input_type_pixels(const std::vector<gpu::HlslProgram>& programs,const frontend::SourceInputLayout& layout){
    GraphicsReferenceOptions options;options.color_format=DXGI_FORMAT_R32G32B32A32_UINT;
    return native_linked_graphics(programs[0],programs[1],options,[&](auto* d,auto* c){const auto elements=layout.descriptions();ImageNativeReference::Ptr<ID3D11InputLayout> input;
        check(SUCCEEDED(d->CreateInputLayout(elements.data(),UINT(elements.size()),programs[0].dxbc.data(),programs[0].dxbc.size(),&input)),"Native mismatched input format creates a layout");
        c->IASetInputLayout(input.Get());D3D11_BUFFER_DESC description{sizeof(input_type_vertices),D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER,0,0,0};
        D3D11_SUBRESOURCE_DATA initial{input_type_vertices,0,0};ImageNativeReference::Ptr<ID3D11Buffer> buffer;
        check(SUCCEEDED(d->CreateBuffer(&description,&initial,&buffer)),"Native input bit reference buffer");auto* handle=buffer.Get();const UINT stride=16,offset=0;c->IASetVertexBuffers(0,1,&handle,&stride,&offset);
    });
}
static void original_vertex_input_types(Device& d,const std::filesystem::path& root){
    for(const char* type:{"float","int","uint"})for(unsigned width=1;width<=4;++width){const auto source=root/(std::string(type)+std::to_string(width));
        const auto programs=input_type_programs(source,type,width);std::vector<unsigned> formats={42,88};
        if(width==4)formats={2,3,4,28,30,31,32,41,42,43,61,62,63,64,87,88};
        for(unsigned format:formats){const auto directory=source/std::to_string(format);std::filesystem::create_directories(directory/"Mods");
            std::filesystem::copy_file(source/"Mods/input.hlsl",directory/"Mods/input.hlsl",std::filesystem::copy_options::overwrite_existing);
            auto elements=std::vector<frontend::SourceVertexElement>{{"ALPHA",0,DXGI_FORMAT(format),0,0}};
            auto layout=std::make_shared<frontend::SourceInputLayout>(elements,programs[0].input_signature);
            const auto expected=native_input_type_pixels(programs,*layout);
            const char* ini=R"ini(namespace = compute
[Constants]
global $phase = 0
o0 = ref set_viewport ResourceOutput
clear = o0 0 0 0 0
[ResourceOutput]
type = Texture2D
width = 4
height = 4
array = 1
mips = 1
msaa = 1
format = R32G32B32A32_UINT
bind_flags = render_target
[ResourceVertices]
type = Buffer
stride = 16
bind_flags = vertex_buffer
data = R32_FLOAT 1 2 3 4 1 2 3 4 1 2 3 4
[ResourceArguments]
type = Buffer
misc_flags = drawindirect_args
data = R32_UINT 3 1 0 0
[CustomShaderMain]
vs = input.hlsl
ps = input.hlsl
cull = none
depth_enable = false
topology = triangle_list
vb0 = ref ResourceVertices
if $phase == 0
draw = 3, 0
else
drawinstancedindirect = ResourceArguments, 0
endif
[Present]
run = CustomShaderMain
$phase = 1
)ini";
            ShaderFixture f(d,directory,ini);f.backend->set_input_layout(layout);f.present();
            auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
            auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
            if(actual!=expected)printf("Input bits differ: %s%u format%u\n",type,width,format);
            check(actual==expected,"Original input register reinterpretation preserves exact native pixels");
            for(unsigned pixel=0;pixel<16;++pixel)check(!memcmp(actual.data()+pixel*16,expected.data(),16),"Typed input reaches every output pixel");
            if(width==4 && format==42){const uint32_t values[]={0x3f800000,0,0,1};check(!memcmp(actual.data(),values,16),"Missing components use fetched integer defaults before reinterpretation");}
            if(width==4 && format==88){uint32_t alpha;memcpy(&alpha,actual.data()+12,4);check(alpha==0x3f800000,"BGRX alpha is float one before signature reinterpretation");}
            if(format==87){elements[0].format=DXGI_FORMAT_B8G8R8X8_UNORM;auto opaque=std::make_shared<frontend::SourceInputLayout>(elements,programs[0].input_signature);
                f.backend->set_input_layout(opaque);f.present();actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
                check(actual==native_input_type_pixels(programs,*opaque) && f.backend->graphics_pipelines()==2,"BGRX fixup distinguishes the same Vulkan BGRA pipeline format");
                f.backend->set_input_layout(layout);f.present();check(f.backend->graphics_pipelines()==2,"BGRA restores its previously cached pipeline");}
            else if(width==4 && format==2){f.present();actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
                check(actual==expected && f.backend->graphics_pipelines()==1,"Indirect typed vertex input reuses the direct pipeline");}
            output.reset();f.release();
        }
    }
}
// Exercise pointer aliases and scalar access chains that optimized DXC usually
// folds into a whole-vector load. The native reference remains the original DXBC.
static gpu::HlslProgram component_input_loads(gpu::HlslProgram program,bool dynamic){
    auto& code=program.spirv;uint32_t input=0,vector=0,pointer=0,scalar=0,uint_type=0,scalar_pointer=0;
    for(const auto& field:program.inputs)if(field.builtin==UINT32_MAX)input=field.spirv_id;
    for(size_t at=5;at<code.size();at+=code[at]>>16){const auto op=SpvOp(code[at]&65535);
        if(op==SpvOpVariable && code[at+2]==input)pointer=code[at+1];
        if(op==SpvOpTypeInt && code[at+2]==32 && !code[at+3])uint_type=code[at+1];}
    for(size_t at=5;at<code.size();at+=code[at]>>16)if(SpvOp(code[at]&65535)==SpvOpTypePointer && code[at+1]==pointer)vector=code[at+3];
    for(size_t at=5;at<code.size();at+=code[at]>>16)if(SpvOp(code[at]&65535)==SpvOpTypeVector && code[at+1]==vector)scalar=code[at+2];
    for(size_t at=5;at<code.size();at+=code[at]>>16)if(SpvOp(code[at]&65535)==SpvOpTypePointer && code[at+2]==SpvStorageClassInput && code[at+3]==scalar)scalar_pointer=code[at+1];
    check(input && vector && pointer && scalar && uint_type,"Component-load diagnostic identifies original input types");
    uint32_t next=code[3];std::vector<uint32_t> declarations,out(code.begin(),code.begin()+5);
    auto emit=[](auto& target,SpvOp op,std::initializer_list<uint32_t> args){target.push_back((uint32_t(args.size()+1)<<16)|op);target.insert(target.end(),args.begin(),args.end());};
    if(!scalar_pointer){scalar_pointer=next++;emit(declarations,SpvOpTypePointer,{scalar_pointer,SpvStorageClassInput,scalar});}
    uint32_t indices[4];for(unsigned c=0;c<4;++c){indices[c]=next++;emit(declarations,SpvOpConstant,{uint_type,indices[c],c});}
    bool inserted=false;unsigned loads=0;
    for(size_t at=5;at<code.size();){const uint32_t count=code[at]>>16;const auto op=SpvOp(code[at]&65535);
        if(!inserted && op==SpvOpFunction){out.insert(out.end(),declarations.begin(),declarations.end());inserted=true;}
        if(op==SpvOpLoad && code[at+3]==input){check(code[at+1]==vector && count==4,"Component diagnostic requires an ordinary vector load");++loads;
            const auto alias=next++;emit(out,SpvOpCopyObject,{pointer,alias,input});uint32_t parts[4];
            for(unsigned c=0;c<4;++c){uint32_t index=indices[c];if(dynamic){index=next++;emit(out,SpvOpIAdd,{uint_type,index,indices[c],indices[0]});}
                const auto address=next++;parts[c]=next++;emit(out,SpvOpAccessChain,{scalar_pointer,address,alias,index});emit(out,SpvOpLoad,{scalar,parts[c],address});}
            emit(out,SpvOpCompositeConstruct,{vector,code[at+2],parts[0],parts[1],parts[2],parts[3]});
        }else out.insert(out.end(),code.begin()+at,code.begin()+at+count);at+=count;
    }
    check(loads && inserted,"Component diagnostic rewrites actual executable loads");out[3]=next;code=std::move(out);return program;
}
static void vertex_input_lowering_guards(Device& d,const std::filesystem::path& root){
    auto programs=input_type_programs(root,"uint",4);uint32_t location=0;for(const auto& input:programs[0].inputs)if(input.builtin==UINT32_MAX)location=input.location;
    gpu::ResourceCommands resources(d.resource);gpu::ShaderCommands shaders(resources,shader_host(d));
    gpu::GraphicsDescription state;state.colors={VK_FORMAT_R32G32B32A32_UINT};state.blend.resize(1);state.blend[0].colorWriteMask=15;
    state.streams={{0,16,VK_VERTEX_INPUT_RATE_VERTEX}};state.attributes={{location,0,VK_FORMAT_B8G8R8A8_UNORM,0}};
    refuses([&]{shaders.compile_graphics(programs,state);},"Numeric input mismatch needs explicit source lowering");
    state.input_conversions={{location,1,true}};
    auto layout=frontend::SourceInputLayout({{"ALPHA",0,DXGI_FORMAT_B8G8R8X8_UNORM,0,0}},programs[0].input_signature);
    const auto expected=native_input_type_pixels(programs,layout);const auto original=programs[0].spirv;
    gpu::ImageDescription image;image.format=state.colors[0];image.extent={4,4,1};image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto target=resources.create_image(image);auto view=resources.view(target,VK_IMAGE_VIEW_TYPE_2D,image.format,target->full_range());
    auto vertices=resources.create(sizeof(input_type_vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,input_type_vertices,sizeof(input_type_vertices));
    gpu::GraphicsDraw draw;draw.colors={view};draw.extent={4,4};draw.viewport={0,4,4,-4,0,1};draw.scissor={{0,0},{4,4}};draw.count=3;draw.vertices={{0,vertices,0}};
    for(bool dynamic:{false,true}){auto aliased=programs;aliased[0]=component_input_loads(aliased[0],dynamic);auto pipeline=shaders.compile_graphics(aliased,state);
        shaders.draw(pipeline,{},draw);check(resources.readback(d.queue,target,image_region(target))==expected,"Aliased scalar input loads preserve bitcasts and BGRX alpha");}
    check(programs[0].spirv==original,"Vertex conversion leaves the source executable immutable");
    auto bad=state;bad.input_conversions.push_back(bad.input_conversions[0]);refuses([&]{shaders.compile_graphics(programs,bad);},"Repeated vertex conversion refuses before creation");
    bad=state;bad.input_conversions[0].location=31;refuses([&]{shaders.compile_graphics(programs,bad);},"Absent input conversion refuses before creation");
    bad=state;bad.input_conversions[0].scalar=2;refuses([&]{shaders.compile_graphics(programs,bad);},"Conversion scalar class must match actual fetch");
    auto broken=programs[0];broken.spirv[3]=0;refuses([&]{gpu::lower_vertex_inputs(broken,state.input_conversions);},"Lowering bounds malformed module identity");
    refuses([&]{gpu::lower_vertex_inputs(programs[1],state.input_conversions);},"Input assembler lowering never targets a pixel shader");
}
