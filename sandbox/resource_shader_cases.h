static std::vector<uint8_t> native_parameter_shader(const gpu::HlslProgram& program,const std::vector<uint8_t>& source){
    ImageNativeReference native;const UINT count=UINT(source.size()/16);
    ImageNativeReference::Ptr<ID3D11Texture1D> texture;
    D3D11_TEXTURE1D_DESC image{count,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
    D3D11_SUBRESOURCE_DATA pixels{source.data(),UINT(source.size()),0};
    check(SUCCEEDED(native.device->CreateTexture1D(&image,&pixels,&texture)),"Native HLSL input texture");
    ImageNativeReference::Ptr<ID3D11ShaderResourceView> srv;
    check(SUCCEEDED(native.device->CreateShaderResourceView(texture.Get(),nullptr,&srv)),"Native HLSL input view");
    D3D11_BUFFER_DESC desc{UINT(source.size()),D3D11_USAGE_DEFAULT,D3D11_BIND_UNORDERED_ACCESS,0,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16};
    ImageNativeReference::Ptr<ID3D11Buffer> output,readback;
    check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&output)),"Native HLSL output buffer");
    ImageNativeReference::Ptr<ID3D11UnorderedAccessView> uav;
    check(SUCCEEDED(native.device->CreateUnorderedAccessView(output.Get(),nullptr,&uav)),"Native HLSL output view");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&readback)),"Native HLSL readback buffer");
    ImageNativeReference::Ptr<ID3D11ComputeShader> shader;
    check(SUCCEEDED(native.device->CreateComputeShader(program.dxbc.data(),program.dxbc.size(),nullptr,&shader)),"Native original compiled compute program");
    auto* source_view=srv.Get();auto* destination_view=uav.Get();
    native.context->CSSetShader(shader.Get(),nullptr,0);native.context->CSSetShaderResources(120,1,&source_view);
    native.context->CSSetUnorderedAccessViews(3,1,&destination_view,nullptr);native.context->Dispatch((count+31)/32,1,1);
    native.context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(native.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Native original compute readback");
    std::vector<uint8_t> result(source.size());memcpy(result.data(),mapped.pData,result.size());native.context->Unmap(readback.Get(),0);return result;
}
static void shader_compiler_gpu(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"parameters.hlsl",R"hlsl(Texture1D<float4> Parameters : register(t120);
RWStructuredBuffer<uint4> Output : register(u3);
[numthreads(32,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint count; Parameters.GetDimensions(count);
    if (id.x < count) Output[id.x] = asuint(Parameters.Load(int2(id.x,0)));
})hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"parameters.hlsl";request.working_directory=root;gpu::HlslProgram program;std::string why;
    if(!compiler.compile(request,program,why))throw std::runtime_error(why);++checks;
    check(program.bindings.size()==2 && program.threads[0]==32,"Reflected shader matches native test consumer contract");
    uint32_t sampled=UINT32_MAX,storage=UINT32_MAX;
    for(const auto& b:program.bindings){if(b.source_type=='t' && b.slot==120)sampled=b.binding;if(b.source_type=='u' && b.slot==3)storage=b.binding;}
    check(sampled!=UINT32_MAX && storage!=UINT32_MAX && sampled!=storage,"Original t/u slots map to independent reflected Vulkan bindings");
    ParameterFixture f(d,root/"parameters","[Constants]\nx = -0\ny = 0.25\nw64 = 90\n");
    const auto input=f.read();const auto expected=native_parameter_shader(program,input);
    {ParameterConsumer consumer(d);consumer.init(program.spirv,sampled,storage);
        const auto actual=consumer.read(f.backend->commands(),f.backend->ini_parameters().image_view);
        check(actual==expected && actual==input,"Original compiled DX11 and remapped Vulkan HLSL produce identical GPU bytes");}
    f.release();
}
