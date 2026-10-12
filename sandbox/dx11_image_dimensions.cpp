// Headless observation of DX11 shader/view dimension mismatch. No injection,
// installed files, game access or global settings. Mismatch cases are invalid API
// usage; their measured output is evidence about this runtime, not a guarantee.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>
using Microsoft::WRL::ComPtr;
static void require(HRESULT hr,const char* op){if(FAILED(hr))throw std::runtime_error(std::string(op)+": "+std::to_string(static_cast<unsigned>(hr)));}
#define DX(call) require((call),#call)
static ComPtr<ID3DBlob> compile(const char* text,const char* target)
{
    ComPtr<ID3DBlob> out,errors;
    const auto hr=D3DCompile(text,strlen(text),"dimension-fixture",nullptr,nullptr,"main",target,
                            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&out,&errors);
    if(errors)fprintf(stderr,"COMPILER: %s\n",static_cast<const char*>(errors->GetBufferPointer()));
    require(hr,"compile shader");return out;
}
int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    bool warp=false,debug=false;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--warp"))warp=true;else if(!strcmp(argv[i],"--debug"))debug=true;else return 2;}
    try{
        ComPtr<IDXGIFactory1> factory;DX(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 chosen{};
        if(!warp)for(UINT i=0;;++i){
            ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 desc{};DX(candidate->GetDesc1(&desc));
            if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && (!adapter || desc.DedicatedVideoMemory>chosen.DedicatedVideoMemory)){adapter=candidate;chosen=desc;}
        }
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0;
        const auto hr=D3D11CreateDevice(adapter.Get(),warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                         debug?D3D11_CREATE_DEVICE_DEBUG:0,&requested,1,D3D11_SDK_VERSION,&device,&level,&ctx);
        if(debug && hr==DXGI_ERROR_SDK_COMPONENT_MISSING){printf("debug_runtime_unavailable\n");return 3;}require(hr,"create device");
        printf("DEVICE warp=%d debug=%d vendor=%u device=%u feature=%u\n",warp,debug,chosen.VendorId,chosen.DeviceId,level);
        ComPtr<ID3D11InfoQueue> info;if(debug)DX(device.As(&info));
        const char* vs=R"(float4 main(uint id:SV_VertexID):SV_Position {
            float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); })";
        const char* ps2=R"(Texture2D<float4> t:register(t0);SamplerState s:register(s0);
            cbuffer C:register(b0){float4 uvw;} float4 main():SV_Target{return t.SampleLevel(s,uvw.xy,0);})";
        const char* ps3=R"(Texture3D<float4> t:register(t0);SamplerState s:register(s0);
            cbuffer C:register(b0){float4 uvw;} float4 main():SV_Target{return t.SampleLevel(s,uvw.xyz,0);})";
        auto vs_code=compile(vs,"vs_5_0"),ps2_code=compile(ps2,"ps_5_0"),ps3_code=compile(ps3,"ps_5_0");
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel2,pixel3;
        DX(device->CreateVertexShader(vs_code->GetBufferPointer(),vs_code->GetBufferSize(),nullptr,&vertex));
        DX(device->CreatePixelShader(ps2_code->GetBufferPointer(),ps2_code->GetBufferSize(),nullptr,&pixel2));
        DX(device->CreatePixelShader(ps3_code->GetBufferPointer(),ps3_code->GetBufferSize(),nullptr,&pixel3));
        const std::array<uint32_t,4> red={0xff0000ff,0xff0000ff,0xff0000ff,0xff0000ff};
        const std::array<uint32_t,8> blue={0xffff0000,0xffff0000,0xffff0000,0xffff0000,0xffff0000,0xffff0000,0xffff0000,0xffff0000};
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data2{red.data(),8,16},data3{blue.data(),8,16};
        ComPtr<ID3D11Texture2D> texture2;DX(device->CreateTexture2D(&td,&data2,&texture2));
        D3D11_TEXTURE3D_DESC volume{};volume.Width=volume.Height=volume.Depth=2;volume.MipLevels=1;
        volume.Format=td.Format;volume.Usage=td.Usage;volume.BindFlags=td.BindFlags;
        ComPtr<ID3D11Texture3D> texture3;DX(device->CreateTexture3D(&volume,&data3,&texture3));
        ComPtr<ID3D11ShaderResourceView> view2,view3;
        DX(device->CreateShaderResourceView(texture2.Get(),nullptr,&view2));DX(device->CreateShaderResourceView(texture3.Get(),nullptr,&view3));
        td.Width=td.Height=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;DX(device->CreateTexture2D(&td,nullptr,&target));
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;DX(device->CreateTexture2D(&td,nullptr,&staging));
        ComPtr<ID3D11RenderTargetView> rtv;DX(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;DX(device->CreateBuffer(&cb,nullptr,&constants));
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;DX(device->CreateSamplerState(&sd,&sampler));
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;DX(device->CreateRasterizerState(&rd,&raster));
        D3D11_VIEWPORT viewport{0,0,1,1,0,1};ctx->RSSetViewports(1,&viewport);ctx->RSSetState(raster.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vertex.Get(),nullptr,0);
        ctx->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);ctx->PSSetSamplers(0,1,sampler.GetAddressOf());
        ctx->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        auto draw=[&](const char* name,ID3D11PixelShader* shader,ID3D11ShaderResourceView* view,float z){
            if(info)info->ClearStoredMessages();const float clear[]={0,1,0,1};ctx->ClearRenderTargetView(rtv.Get(),clear);
            float uvw[]={.5f,.5f,z,0};ctx->UpdateSubresource(constants.Get(),0,nullptr,uvw,0,0);
            ctx->PSSetShader(shader,nullptr,0);ctx->PSSetShaderResources(0,1,&view);ctx->Draw(3,0);
            ComPtr<ID3D11ShaderResourceView> bound;ctx->PSGetShaderResources(0,1,&bound);
            printf("BOUND %s %d\n",name,bound.Get()==view);
            ctx->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};DX(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
            std::array<uint8_t,4> rgba{};memcpy(rgba.data(),mapped.pData,4);ctx->Unmap(staging.Get(),0);
            printf("PIXEL %s %u %u %u %u\n",name,rgba[0],rgba[1],rgba[2],rgba[3]);
            if(info)for(UINT64 n=0;n<info->GetNumStoredMessages();++n){
                SIZE_T size=0;DX(info->GetMessage(n,nullptr,&size));std::vector<uint8_t> bytes(size);
                auto message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());DX(info->GetMessage(n,message,&size));
                printf("DIAGNOSTIC %s %u %s\n",name,message->ID,message->pDescription);
            }
            return rgba;
        };
        bool ok=true;
        ok &= draw("2d_match",pixel2.Get(),view2.Get(),.5f)==std::array<uint8_t,4>{255,0,0,255};
        ok &= draw("3d_match",pixel3.Get(),view3.Get(),.5f)==std::array<uint8_t,4>{0,0,255,255};
        ok &= draw("2d_null",pixel2.Get(),nullptr,.5f)==std::array<uint8_t,4>{0,0,0,0};
        ok &= draw("3d_null",pixel3.Get(),nullptr,.5f)==std::array<uint8_t,4>{0,0,0,0};
        draw("3d_shader_2d_view_z0",pixel3.Get(),view2.Get(),0);
        draw("3d_shader_2d_view_z05",pixel3.Get(),view2.Get(),.5f);
        draw("3d_shader_2d_view_z2",pixel3.Get(),view2.Get(),2);
        draw("2d_shader_3d_view",pixel2.Get(),view3.Get(),.5f);
        ctx->ClearState();ctx->Flush();printf("controls_passed=%d\n",ok);return ok?0:1;
    }catch(const std::exception& e){fprintf(stderr,"FAILED: %s\n",e.what());return 1;}
}
