#include "dds.h"
#include <algorithm>
#include <utility>

namespace efmi::assets {
namespace {
constexpr uint32_t fourcc(char a,char b,char c,char d)
{
    return uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24);
}
uint32_t word(const uint8_t* p)
{
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
struct Format { VkFormat vk=VK_FORMAT_UNDEFINED;uint32_t block=0,bytes=0; };
Format dxgi(uint32_t format)
{
    switch(format){
    case 28:return {VK_FORMAT_R8G8B8A8_UNORM,1,4};
    case 29:return {VK_FORMAT_R8G8B8A8_SRGB,1,4};
    case 87:return {VK_FORMAT_B8G8R8A8_UNORM,1,4};
    case 91:return {VK_FORMAT_B8G8R8A8_SRGB,1,4};
    case 71:return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK,4,8};
    case 72:return {VK_FORMAT_BC1_RGBA_SRGB_BLOCK,4,8};
    case 74:return {VK_FORMAT_BC2_UNORM_BLOCK,4,16};
    case 75:return {VK_FORMAT_BC2_SRGB_BLOCK,4,16};
    case 77:return {VK_FORMAT_BC3_UNORM_BLOCK,4,16};
    case 78:return {VK_FORMAT_BC3_SRGB_BLOCK,4,16};
    case 80:return {VK_FORMAT_BC4_UNORM_BLOCK,4,8};
    case 81:return {VK_FORMAT_BC4_SNORM_BLOCK,4,8};
    case 83:return {VK_FORMAT_BC5_UNORM_BLOCK,4,16};
    case 84:return {VK_FORMAT_BC5_SNORM_BLOCK,4,16};
    case 95:return {VK_FORMAT_BC6H_UFLOAT_BLOCK,4,16};
    case 96:return {VK_FORMAT_BC6H_SFLOAT_BLOCK,4,16};
    case 98:return {VK_FORMAT_BC7_UNORM_BLOCK,4,16};
    case 99:return {VK_FORMAT_BC7_SRGB_BLOCK,4,16};
    default:return {};
    }
}
}

VkFormat image_vk_format(uint32_t format)
{
    switch(format){
    case 61:return VK_FORMAT_R8_UNORM;
    case 56:return VK_FORMAT_R16_UNORM;
    case 11:return VK_FORMAT_R16G16B16A16_UNORM;
    default:return dxgi(format).vk;
    }
}
VkFormat srgb_format(VkFormat format)
{
    switch(format){
    case VK_FORMAT_R8G8B8A8_UNORM:return VK_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:return VK_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case VK_FORMAT_BC2_UNORM_BLOCK:return VK_FORMAT_BC2_SRGB_BLOCK;
    case VK_FORMAT_BC3_UNORM_BLOCK:return VK_FORMAT_BC3_SRGB_BLOCK;
    case VK_FORMAT_BC7_UNORM_BLOCK:return VK_FORMAT_BC7_SRGB_BLOCK;
    default:return format;
    }
}
bool decode_dds(const uint8_t* data,size_t size,DdsImage& output,std::string& reason)
{
    return decode_dds_header(data,size,size,output,reason);
}
bool decode_dds_header(const uint8_t* data,size_t available,uint64_t file_size,DdsImage& output,std::string& reason)
{
    reason.clear();
    auto fail=[&](const char* text){reason=text;return false;};
    if(!data || file_size<128 || file_size>(128ull<<20))return fail("DDS file size is outside budget");
    const size_t size=static_cast<size_t>(file_size);
    if(available<std::min(size,kDdsHeaderBytes))return fail("DDS header bytes are missing");
    if(word(data)!=fourcc('D','D','S',' ') || word(data+4)!=124 || word(data+76)!=32)
        return fail("DDS magic/header size mismatch");
    const auto flags=word(data+8),width=word(data+16),height=word(data+12);
    if((flags&0x1007)!=0x1007 || !(word(data+108)&0x1000))return fail("DDS required texture flags missing");
    if(!width || !height || width>16384 || height>16384)return fail("DDS dimensions are outside budget");
    if(word(data+24)>1 || word(data+112)!=0 || (flags&0x800000))
        return fail("DDS arrays, cubes and volumes require another loader");
    uint32_t count=word(data+28);if(!count)count=1;
    uint32_t maximum=1;for(auto n=std::max(width,height);n>1;n>>=1)++maximum;
    if(count>maximum || (count>1 && !(flags&0x20000)))return fail("DDS mip count is inconsistent");
    Format format;size_t offset=128;
    const auto pf_flags=word(data+80),code=word(data+84);
    if(pf_flags&4){
        if(code==fourcc('D','X','1','0')){
            if(size<148)return fail("DDS DX10 header is truncated");
            if(word(data+132)!=3 || word(data+136)!=0 || word(data+140)!=1)
                return fail("DDS DX10 resource is not a single ordinary 2D texture");
            if(word(data+144)!=0 && word(data+144)!=1 && word(data+144)!=3)
                return fail("DDS non-straight alpha interpretation is unsupported");
            format=dxgi(word(data+128));offset=148;
        }else{
            switch(code){
            case fourcc('D','X','T','1'):format=dxgi(71);break;
            case fourcc('D','X','T','3'):format=dxgi(74);break;
            case fourcc('D','X','T','5'):format=dxgi(77);break;
            case fourcc('A','T','I','1'):case fourcc('B','C','4','U'):format=dxgi(80);break;
            case fourcc('B','C','4','S'):format=dxgi(81);break;
            case fourcc('A','T','I','2'):case fourcc('B','C','5','U'):format=dxgi(83);break;
            case fourcc('B','C','5','S'):format=dxgi(84);break;
            default:break;
            }
        }
    }else if((pf_flags&0x41)==0x41 && word(data+88)==32 && word(data+96)==0xff00 && word(data+104)==0xff000000){
        if(word(data+92)==0xff && word(data+100)==0xff0000)format=dxgi(28);
        else if(word(data+92)==0xff0000 && word(data+100)==0xff)format=dxgi(87);
    }
    if(format.vk==VK_FORMAT_UNDEFINED)return fail("DDS format is unsupported or typeless");
    DdsImage candidate;candidate.format=format.vk;candidate.width=width;candidate.height=height;
    auto w=width,h=height;
    for(uint32_t mip=0;mip<count;++mip){
        const uint64_t row_bytes=uint64_t((w+format.block-1)/format.block)*format.bytes;
        const uint64_t bytes=row_bytes*((h+format.block-1)/format.block);
        if(mip==0 && (flags&8) && word(data+20)!=row_bytes)
            return fail("DDS padded row pitch is unsupported");
        if(mip==0 && (flags&0x80000) && word(data+20)!=bytes)
            return fail("DDS linear size disagrees with its format and dimensions");
        if(offset>size || bytes>size-offset)return fail("DDS mip payload is truncated");
        candidate.levels.push_back({w,h,offset,static_cast<size_t>(bytes)});
        offset+=static_cast<size_t>(bytes);w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    if(offset!=size)return fail("DDS trailing payload is unsupported");
    output=std::move(candidate);return true;
}
}
