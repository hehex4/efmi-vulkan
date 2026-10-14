// Compare the CPU bridge with the unmodified source merge methods calling an
// actual D3D11 device. This validates descriptions, not Vulkan state emission.
static bool equal_source_blend(const D3D11_BLEND_DESC& a,const D3D11_BLEND_DESC& b){
    if(a.AlphaToCoverageEnable!=b.AlphaToCoverageEnable || a.IndependentBlendEnable!=b.IndependentBlendEnable)return false;
    for(unsigned i=0;i<8;++i){const auto& x=a.RenderTarget[i];const auto& y=b.RenderTarget[i];
        if(x.BlendEnable!=y.BlendEnable || x.SrcBlend!=y.SrcBlend || x.DestBlend!=y.DestBlend || x.BlendOp!=y.BlendOp ||
            x.SrcBlendAlpha!=y.SrcBlendAlpha || x.DestBlendAlpha!=y.DestBlendAlpha || x.BlendOpAlpha!=y.BlendOpAlpha || x.RenderTargetWriteMask!=y.RenderTargetWriteMask)return false;}
    return true;
}
static bool equal_source_depth(const D3D11_DEPTH_STENCIL_DESC& a,const D3D11_DEPTH_STENCIL_DESC& b){
    auto face=[](const auto& x,const auto& y){return x.StencilFunc==y.StencilFunc && x.StencilFailOp==y.StencilFailOp &&
        x.StencilDepthFailOp==y.StencilDepthFailOp && x.StencilPassOp==y.StencilPassOp;};
    return a.DepthEnable==b.DepthEnable && a.DepthWriteMask==b.DepthWriteMask && a.DepthFunc==b.DepthFunc && a.StencilEnable==b.StencilEnable &&
        a.StencilReadMask==b.StencilReadMask && a.StencilWriteMask==b.StencilWriteMask && face(a.FrontFace,b.FrontFace) && face(a.BackFace,b.BackFace);
}
static bool equal_source_raster(const D3D11_RASTERIZER_DESC& a,const D3D11_RASTERIZER_DESC& b){
    return a.FillMode==b.FillMode && a.CullMode==b.CullMode && a.FrontCounterClockwise==b.FrontCounterClockwise &&
        a.DepthBias==b.DepthBias && a.DepthBiasClamp==b.DepthBiasClamp && a.SlopeScaledDepthBias==b.SlopeScaledDepthBias &&
        a.DepthClipEnable==b.DepthClipEnable && a.ScissorEnable==b.ScissorEnable && a.MultisampleEnable==b.MultisampleEnable && a.AntialiasedLineEnable==b.AntialiasedLineEnable;
}
static void original_graphics_state_descriptions(){
    ImageNativeReference native;auto* device=native.device.Get();
    auto defaults=frontend::source_graphics_defaults();
    check(defaults.blend.RenderTarget[0].RenderTargetWriteMask==15 && defaults.depth.DepthEnable &&
        defaults.depth.DepthFunc==D3D11_COMPARISON_LESS && defaults.raster.CullMode==D3D11_CULL_BACK &&
        defaults.raster.DepthClipEnable && defaults.blend_factor[0]==1 && !defaults.pixel_samplers[0],"Source null-state defaults");
    auto inherited=defaults;inherited.blend.IndependentBlendEnable=TRUE;inherited.blend.AlphaToCoverageEnable=TRUE;
    inherited.blend.RenderTarget[2].RenderTargetWriteMask=3;inherited.blend_factor={{.1f,.2f,.3f,.4f}};inherited.sample_mask=0x12345678;
    inherited.depth.DepthFunc=D3D11_COMPARISON_GREATER;inherited.depth.StencilEnable=TRUE;inherited.depth.StencilReadMask=0x5a;
    inherited.stencil_ref=0x98;inherited.raster.FrontCounterClockwise=TRUE;inherited.raster.CullMode=D3D11_CULL_FRONT;
    inherited.raster.DepthBias=17;inherited.raster.ScissorEnable=TRUE;inherited.topology=D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxLOD=FLT_MAX;inherited.pixel_samplers[3]=sampler;
    ImageNativeReference::Ptr<ID3D11BlendState> blend;
    ImageNativeReference::Ptr<ID3D11DepthStencilState> depth;
    ImageNativeReference::Ptr<ID3D11RasterizerState> raster;
    check(SUCCEEDED(device->CreateBlendState(&inherited.blend,&blend)) && SUCCEEDED(device->CreateDepthStencilState(&inherited.depth,&depth)) &&
        SUCCEEDED(device->CreateRasterizerState(&inherited.raster,&raster)),"Native inherited graphics states");
    auto initialize=[&](CustomShader& shader,int mode){
        shader.blend_override=shader.depth_stencil_override=shader.rs_override=mode;
        shader.blend_desc=defaults.blend;shader.blend_desc.IndependentBlendEnable=TRUE;shader.blend_desc.RenderTarget[0].RenderTargetWriteMask=5;
        memset(&shader.blend_mask,0xff,sizeof(shader.blend_mask));shader.blend_mask.RenderTarget[0].RenderTargetWriteMask=0;
        shader.blend_mask.AlphaToCoverageEnable=0;shader.blend_sample_mask=0xff00ff00;shader.blend_sample_mask_merge_mask=0x00ff00ff;
        for(unsigned i=0;i<4;++i){shader.blend_factor[i]=float(i+2);shader.blend_factor_merge_mask[i]=(i%2)?0:1;}
        shader.depth_stencil_desc=defaults.depth;shader.depth_stencil_desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        memset(&shader.depth_stencil_mask,0xff,sizeof(shader.depth_stencil_mask));shader.depth_stencil_mask.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
        shader.depth_stencil_mask.StencilReadMask=0x0f;shader.stencil_ref=0x72;shader.stencil_ref_mask=0x0f;
        shader.rs_desc=defaults.raster;shader.rs_desc.CullMode=D3D11_CULL_NONE;
        memset(&shader.rs_mask,0xff,sizeof(shader.rs_mask));shader.rs_mask.CullMode=D3D11_CULL_MODE(0);
        shader.rs_mask.DepthBias=0;shader.rs_mask.ScissorEnable=0;
    };
    for(bool null_state:{false,true})for(int mode:{0,1,2}){
        CustomShader shader;initialize(shader,mode);const auto& before=null_state?defaults:inherited;
        const auto actual=frontend::source_graphics_override(shader,before);
        if(!mode){check(equal_source_blend(actual.blend,before.blend) && equal_source_depth(actual.depth,before.depth) &&
            equal_source_raster(actual.raster,before.raster) && actual.blend_factor==before.blend_factor &&
            actual.sample_mask==before.sample_mask && actual.stencil_ref==before.stencil_ref,"Absent overrides preserve all source state");continue;}
        if(mode==2){auto factors=before.blend_factor;
            shader.merge_blend_states(null_state?nullptr:blend.Get(),factors.data(),before.sample_mask,device);
            shader.merge_depth_stencil_states(null_state?nullptr:depth.Get(),before.stencil_ref,device);
            shader.merge_rasterizer_states(null_state?nullptr:raster.Get(),device);
            check(shader.blend_state && shader.depth_stencil_state && shader.rs_state,"Original merged descriptions create native states");}
        check(equal_source_blend(actual.blend,shader.blend_desc) && equal_source_depth(actual.depth,shader.depth_stencil_desc) &&
            equal_source_raster(actual.raster,shader.rs_desc),"Source description merge equals the original native implementation");
        check(!memcmp(actual.blend_factor.data(),shader.blend_factor,sizeof(shader.blend_factor)) && actual.sample_mask==shader.blend_sample_mask &&
            actual.stencil_ref==shader.stencil_ref,"Source merge preserves blend factors, sample masks and stencil masks");
        check(actual.topology==before.topology && bool(actual.pixel_samplers[3])==bool(before.pixel_samplers[3]),"Unspecified topology and pixel samplers inherit");
    }
    CustomShader shader;initialize(shader,0);shader.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;shader.sampler_override=1;shader.sampler_desc=sampler;
    shader.sampler_desc.Filter=D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;shader.sampler_desc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;shader.sampler_desc.MaxLOD=1;
    auto overridden=frontend::source_graphics_override(shader,inherited);
    check(overridden.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP && overridden.pixel_samplers[0] &&
        overridden.pixel_samplers[0]->Filter==shader.sampler_desc.Filter && overridden.pixel_samplers[3] && !overridden.pixel_samplers[1],"Original sampler override touches PS slot zero only");
    check(!inherited.pixel_samplers[0] && inherited.topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Source override leaves caller snapshot immutable");
    shader.blend_override=3;refuses([&]{frontend::source_graphics_override(shader,inherited);},"Invalid source state mode refuses");
}
