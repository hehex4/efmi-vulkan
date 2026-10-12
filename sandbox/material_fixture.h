#pragma once
#include "src/spirv.h"
#include <utility>

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
// An out-of-range sample with a white border, width/mip queries, and a second
// image that must remain readable when the first resource is explicitly null.
inline std::vector<uint32_t> null_probe()
{
    using namespace spv;Builder b;
    auto main=b.id(),output=b.id(),source=b.id(),other=b.id(),sampler=b.id();
    auto tv=b.id(),tf=b.id(),f=b.id(),v2=b.id(),v4=b.id(),ti=b.id(),i2=b.id();
    auto img=b.id(),sam=b.id(),sampled=b.id(),pi=b.id(),ps=b.id(),po=b.id();
    auto zi=b.id(),zf=b.id(),two=b.id(),uv=b.id(),xy=b.id();
    auto label=b.id(),im=b.id(),sm=b.id(),combined=b.id(),color=b.id(),widths=b.id(),levels=b.id();
    auto r=b.id(),w=b.id(),wf=b.id(),lf=b.id(),oi=b.id(),oc=b.id(),blue=b.id(),result=b.id();
    b.op(OpCapability,{CapabilityShader});b.op(OpCapability,{50}); // ImageQuery.
    b.op(OpMemoryModel,{AddressingLogical,MemoryGLSL450});b.entry_point(ModelFragment,main,"main",{output});
    b.op(OpExecutionMode,{main,ModeOriginUpperLeft});b.op(OpDecorate,{output,DecorationLocation,0});
    for(auto pair:{std::pair<uint32_t,uint32_t>{source,6},{sampler,7},{other,8}}){
        b.op(OpDecorate,{pair.first,DecorationDescriptorSet,1});b.op(OpDecorate,{pair.first,DecorationBinding,pair.second});
    }
    b.op(OpTypeVoid,{tv});b.op(OpTypeFunction,{tf,tv});b.op(OpTypeFloat,{f,32});
    b.op(OpTypeVector,{v2,f,2});b.op(OpTypeVector,{v4,f,4});b.op(OpTypeInt,{ti,32,1});b.op(OpTypeVector,{i2,ti,2});
    b.op(OpTypeImage,{img,f,1,2,0,0,1,0});b.op(static_cast<Op>(26),{sam});b.op(static_cast<Op>(27),{sampled,img});
    b.op(OpTypePointer,{pi,StorageUniformConstant,img});b.op(OpTypePointer,{ps,StorageUniformConstant,sam});
    b.op(OpTypePointer,{po,StorageOutput,v4});
    b.op(OpConstant,{ti,zi,0});b.op(OpConstant,{f,zf,0});b.op(OpConstant,{f,two,0x40000000});
    b.op(OpConstantComposite,{v2,uv,two,two});b.op(OpConstantComposite,{i2,xy,zi,zi});
    b.op(OpVariable,{pi,source,StorageUniformConstant});b.op(OpVariable,{pi,other,StorageUniformConstant});
    b.op(OpVariable,{ps,sampler,StorageUniformConstant});b.op(OpVariable,{po,output,StorageOutput});
    b.op(OpFunction,{tv,main,0,tf});b.op(OpLabel,{label});b.op(OpLoad,{img,im,source});b.op(OpLoad,{sam,sm,sampler});
    b.op(static_cast<Op>(86),{sampled,combined,im,sm});b.op(static_cast<Op>(88),{v4,color,combined,uv,2,zf});
    b.op(static_cast<Op>(103),{i2,widths,im,zi});b.op(static_cast<Op>(106),{ti,levels,im});
    b.op(OpCompositeExtract,{f,r,color,0});b.op(OpCompositeExtract,{ti,w,widths,0});
    b.op(static_cast<Op>(111),{f,wf,w});b.op(static_cast<Op>(111),{f,lf,levels}); // ConvertSToF.
    b.op(OpLoad,{img,oi,other});b.op(static_cast<Op>(95),{v4,oc,oi,xy,2,zi});b.op(OpCompositeExtract,{f,blue,oc,2});
    b.op(OpCompositeConstruct,{v4,result,r,wf,lf,blue});b.op(OpStore,{output,result});
    b.op(OpReturn,{});b.op(OpFunctionEnd,{});return b.finish();
}
}
