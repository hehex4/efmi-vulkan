static void emit_linked(const std::vector<HlslProgram>& programs,const std::filesystem::path& root){
    for(const auto& p:programs){++compiled;std::ofstream file(root/(std::to_string(compiled)+".spv"),std::ios::binary);
        file.write(reinterpret_cast<const char*>(p.spirv.data()),p.spirv.size()*4);check(bool(file),"Export linked executable for independent validation");}
}
template<class F> static void link_refuses(F&& operation,const char* message){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,message);}
static const ShaderInterface& link_interface(const std::vector<ShaderInterface>& values,const char* semantic){
    for(const auto& value:values)if(!_stricmp(value.semantic.c_str(),semantic))return value;throw std::runtime_error("Expected original shader semantic is absent");
}
static void original_graphics_link(HlslCompiler& compiler,const std::filesystem::path& root){
    HlslRequest request;request.working_directory=root;request.source=root/"graphics.hlsl";
    auto make=[&](char stage,const std::string& code){request.stage=stage;write(request.source,code);return compile(compiler,request,root);};
    const auto vertex=make('v',R"hlsl(struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;float2 extra:AAA;};
V main(uint id:SV_VertexID){V v;v.position=float4(id&1,id>>1,0,1);v.extra=7;v.uv=float2(2,3);return v;})hlsl");
    const auto pixel=make('p',"struct V{float4 position:SV_Position;float2 uv:texcoord;}; float4 main(V v):SV_Target {return float4(v.uv,0,1);}");
    const auto& produced=link_interface(vertex.outputs,"TEXCOORD0");const auto& consumed=link_interface(pixel.inputs,"texcoord");
    check(produced.location!=consumed.location,"Separate alphabetic IO assignment does not connect a subset signature");
    // Valid original DXBC interfaces retain matching register/component layout.
    // A trailing source field may be omitted, but separate alphabetic Vulkan
    // location assignment moves that omitted field before the remaining input.
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> vs,ps;
    check(SUCCEEDED(D3DReflect(vertex.dxbc.data(),vertex.dxbc.size(),IID_PPV_ARGS(&vs))) &&
        SUCCEEDED(D3DReflect(pixel.dxbc.data(),pixel.dxbc.size(),IID_PPV_ARGS(&ps))),"Reflect native graphics signatures");
    D3D11_SHADER_DESC vd{},pd{};vs->GetDesc(&vd);ps->GetDesc(&pd);D3D11_SIGNATURE_PARAMETER_DESC original_out{},original_in{};
    bool have_out=false,have_in=false;
    for(UINT i=0;i<vd.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};vs->GetOutputParameterDesc(i,&p);
        if(!_stricmp(p.SemanticName,"TEXCOORD") && p.SemanticIndex==0){original_out=p;have_out=true;}}
    for(UINT i=0;i<pd.InputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};ps->GetInputParameterDesc(i,&p);
        if(!_stricmp(p.SemanticName,"TEXCOORD") && p.SemanticIndex==0){original_in=p;have_in=true;}}
    check(have_out && have_in && original_out.ComponentType==original_in.ComponentType && original_out.Register==original_in.Register &&
        original_out.Mask==original_in.Mask,"Native shader signatures preserve compatible register/component layout");
    auto linked=link_graphics({pixel,vertex});
    check(linked.size()==2 && linked[0].stage==VK_SHADER_STAGE_VERTEX_BIT && linked[1].stage==VK_SHADER_STAGE_FRAGMENT_BIT &&
        link_interface(linked[1].inputs,"texcoord").location==produced.location,"Linker orders stages and relocates the consumer to its source semantic");
    check(vertex.spirv==linked[0].spirv && pixel.spirv!=linked[1].spirv && consumed.location==0,
        "Relocation preserves the input programs and producer executable");
    check(linked[1].dxbc==pixel.dxbc && linked[1].dependencies.size()==pixel.dependencies.size(),"Linking retains native reference and source evidence");emit_linked(linked,root);
    const auto missing=make('p',"float4 main(float2 uv:NORMAL):SV_Target {return float4(uv,0,1);}");
    link_refuses([&]{link_graphics({vertex,missing});},"Missing source semantic refuses graphics linkage");
    link_refuses([&]{link_graphics({pixel});},"Graphics link requires its vertex producer");
    link_refuses([&]{link_graphics({vertex,pixel,vertex});},"Duplicate graphics stages refuse");
    auto forged=pixel;forged.inputs.front().spirv_id=0;
    link_refuses([&]{link_graphics({vertex,forged});},"Missing executable identity refuses stale interface metadata");
    forged=pixel;forged.inputs.front().location=99;
    link_refuses([&]{link_graphics({vertex,forged});},"Metadata locations must agree with the executable");
    const auto wide=make('v',"struct V{float4 p:SV_Position;float4 value:TEXCOORD7;}; V main(uint id:SV_VertexID){V v;v.p=float4(id,0,0,1);v.value=1;return v;}");
    const auto narrow=make('p',"struct V{float4 p:SV_Position;float2 value:TEXCOORD7;};float4 main(V v):SV_Target{return float4(v.value,0,1);}");
    link_refuses([&]{link_graphics({wide,narrow});},"Vector width relaxation requires actual maintenance4 enablement");
    emit_linked(link_graphics({wide,narrow},true),root);
    const auto signed_input=make('p',"struct V{float4 p:SV_Position;nointerpolation uint4 value:TEXCOORD7;};float4 main(V v):SV_Target{return float4(v.value);}");
    link_refuses([&]{link_graphics({wide,signed_input},true);},"maintenance4 does not relax scalar type differences");
    const std::string vtype="struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;float2 extra:AAA;};\n";
    const auto geometry=make('g',vtype+"[maxvertexcount(3)] void main(triangle V p[3],inout TriangleStream<V> stream){stream.Append(p[0]);stream.Append(p[1]);stream.Append(p[2]);}");
    linked=link_graphics({pixel,geometry,vertex});
    check(link_interface(linked[1].inputs,"TEXCOORD0").location==produced.location &&
        link_interface(linked[2].inputs,"texcoord").location==link_interface(linked[1].outputs,"TEXCOORD0").location,
        "Per-vertex geometry arrays connect both adjacent source interfaces");emit_linked(linked,root);
    const std::string patch="struct C {float edge[3]:SV_TessFactor;float inside:SV_InsideTessFactor;float weight:COLOR0;};\n";
    const auto hull=make('h',vtype+patch+R"hlsl(C Patch(InputPatch<V,3> p){C c;c.edge[0]=c.edge[1]=c.edge[2]=c.inside=1;c.weight=p[0].uv.x;return c;}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("Patch")]
V main(InputPatch<V,3> p,uint id:SV_OutputControlPointID){return p[id];})hlsl");
    const auto domain=make('d',vtype+patch+R"hlsl([domain("tri")] V main(C c,float3 bary:SV_DomainLocation,const OutputPatch<V,3> p){
V v;v.position=p[0].position*bary.x+p[1].position*bary.y+p[2].position*bary.z;v.extra=7;v.uv=p[0].uv*c.weight;return v;})hlsl");
    link_refuses([&]{link_graphics({vertex,hull,pixel});},"Hull and domain stages must be paired");
    linked=link_graphics({domain,pixel,hull,vertex});
    check(link_interface(linked[1].outputs,"COLOR0").location==link_interface(linked[2].inputs,"COLOR0").location,
        "Tessellation patch semantics remain separate from per-vertex locations");emit_linked(linked,root);
    const auto array_vertex=make('v',"struct V{float4 p:SV_Position;float2 uv[2]:TEXCOORD0;}; V main(uint id:SV_VertexID){V v;v.p=float4(id,0,0,1);v.uv[0]=2;v.uv[1]=3;return v;}");
    const auto array_pixel=make('p',"struct V{float4 p:SV_Position;float2 uv[2]:TEXCOORD0;};float4 main(V v):SV_Target{return float4(v.uv[0],v.uv[1]);}");
    emit_linked(link_graphics({array_vertex,array_pixel}),root);
    check(vertex.spirv==linked[0].spirv,"Further links do not mutate a retained shared vertex program");
    const auto native_invalid=make('p',"float4 main(float2 uv:TEXCOORD0):SV_Target{return float4(uv,0,1);}");
    link_refuses([&]{link_graphics({vertex,native_invalid});},"Semantic equality cannot repair incompatible original DXBC register layout");
}
