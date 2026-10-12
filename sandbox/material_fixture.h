#pragma once
#include "src/spirv.h"

namespace material_fixture {
// Both stages read the same ordinary image at (1,6). Vertex red moves X;
// fragment outputs RGBA. Moving only the fragment image must not move geometry.
inline std::vector<uint32_t> shader(bool vertex)
{
    using namespace spv;Builder b;
    auto main=b.id(),input=b.id(),output=b.id(),pv=b.id(),image_var=b.id();
    auto t_void=b.id(),t_fn=b.id(),t_f=b.id(),t_v2=b.id(),t_v4=b.id(),t_i=b.id(),t_i2=b.id(),t_img=b.id();
    auto p_img=b.id(),p_in=b.id(),p_out=b.id(),p_v4=b.id(),zero=b.id(),fzero=b.id(),one=b.id(),coord=b.id();
    auto label=b.id(),image=b.id(),texel=b.id(),pos=b.id(),x=b.id(),y=b.id(),red=b.id(),moved=b.id(),value=b.id(),out_ptr=b.id();
    b.op(OpCapability,{CapabilityShader});b.op(OpMemoryModel,{AddressingLogical,MemoryGLSL450});
    if(vertex)b.entry_point(ModelVertex,main,"main",{input,output});
    else{b.entry_point(ModelFragment,main,"main",{output});b.op(OpExecutionMode,{main,ModeOriginUpperLeft});}
    b.op(OpDecorate,{image_var,DecorationDescriptorSet,1});b.op(OpDecorate,{image_var,DecorationBinding,6});
    if(vertex){
        b.op(OpDecorate,{input,DecorationLocation,0});b.op(OpDecorate,{pv,DecorationBlock});
        b.op(OpMemberDecorate,{pv,0,DecorationBuiltIn,BuiltInPosition});
    }else b.op(OpDecorate,{output,DecorationLocation,0});
    b.op(OpTypeVoid,{t_void});b.op(OpTypeFunction,{t_fn,t_void});b.op(OpTypeFloat,{t_f,32});
    b.op(OpTypeVector,{t_v2,t_f,2});b.op(OpTypeVector,{t_v4,t_f,4});
    b.op(OpTypeInt,{t_i,32,1});b.op(OpTypeVector,{t_i2,t_i,2});
    b.op(OpTypeImage,{t_img,t_f,1,0,0,0,1,0});b.op(OpTypePointer,{p_img,StorageUniformConstant,t_img});
    b.op(OpTypePointer,{p_v4,StorageOutput,t_v4});
    b.op(OpConstant,{t_i,zero,0});b.op(OpConstant,{t_f,fzero,0});b.op(OpConstant,{t_f,one,0x3f800000});
    b.op(OpConstantComposite,{t_i2,coord,zero,zero});b.op(OpVariable,{p_img,image_var,StorageUniformConstant});
    if(vertex){
        b.op(OpTypeStruct,{pv,t_v4});b.op(OpTypePointer,{p_out,StorageOutput,pv});b.op(OpTypePointer,{p_in,StorageInput,t_v2});
        b.op(OpVariable,{p_in,input,StorageInput});b.op(OpVariable,{p_out,output,StorageOutput});
    }else b.op(OpVariable,{p_v4,output,StorageOutput});
    b.op(OpFunction,{t_void,main,0,t_fn});b.op(OpLabel,{label});b.op(OpLoad,{t_img,image,image_var});
    b.op(static_cast<Op>(95),{t_v4,texel,image,coord,2,zero}); // OpImageFetch, Lod operand.
    if(vertex){
        b.op(OpLoad,{t_v2,pos,input});b.op(OpCompositeExtract,{t_f,x,pos,0});b.op(OpCompositeExtract,{t_f,y,pos,1});
        b.op(OpCompositeExtract,{t_f,red,texel,0});b.op(OpFAdd,{t_f,moved,x,red});
        b.op(OpCompositeConstruct,{t_v4,value,moved,y,fzero,one});b.op(OpAccessChain,{p_v4,out_ptr,output,zero});
        b.op(OpStore,{out_ptr,value});
    }else b.op(OpStore,{output,texel});
    b.op(OpReturn,{});b.op(OpFunctionEnd,{});return b.finish();
}
}
