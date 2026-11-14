#pragma once
// CPU encoders consume only completed staging data; they issue no device work.
#include "resources.h"
#include "json.h"
#include <filesystem>
#include <fstream>
#include <wincodec.h>
#include <wrl/client.h>
#include <dxgiformat.h>
#include <iomanip>
#include <cstring>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace efvk::analysis_export {
struct Image { VkFormat format=VK_FORMAT_UNDEFINED; uint32_t width=0,height=0; };

inline DXGI_FORMAT dxgi(VkFormat format) {
    switch(format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VK_FORMAT_R32G32B32A32_UINT:return DXGI_FORMAT_R32G32B32A32_UINT;
    case VK_FORMAT_R32G32B32A32_SINT:return DXGI_FORMAT_R32G32B32A32_SINT;
    case VK_FORMAT_R32G32B32_SFLOAT:return DXGI_FORMAT_R32G32B32_FLOAT;
    case VK_FORMAT_R32G32B32_UINT:return DXGI_FORMAT_R32G32B32_UINT;
    case VK_FORMAT_R32G32B32_SINT:return DXGI_FORMAT_R32G32B32_SINT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R16G16B16A16_UNORM:return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16A16_UINT:return DXGI_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16A16_SNORM:return DXGI_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16G16B16A16_SINT:return DXGI_FORMAT_R16G16B16A16_SINT;
    case VK_FORMAT_R32G32_SFLOAT:return DXGI_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R32G32_UINT:return DXGI_FORMAT_R32G32_UINT;
    case VK_FORMAT_R32G32_SINT:return DXGI_FORMAT_R32G32_SINT;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:return DXGI_FORMAT_R10G10B10A2_UINT;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:return DXGI_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_R8G8B8A8_UNORM:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case VK_FORMAT_R8G8B8A8_UINT:return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R8G8B8A8_SNORM:return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8A8_SINT:return DXGI_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_R16G16_SFLOAT:return DXGI_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R16G16_UNORM:return DXGI_FORMAT_R16G16_UNORM;
    case VK_FORMAT_R16G16_UINT:return DXGI_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16G16_SNORM:return DXGI_FORMAT_R16G16_SNORM;
    case VK_FORMAT_R16G16_SINT:return DXGI_FORMAT_R16G16_SINT;
    case VK_FORMAT_R32_SFLOAT:return DXGI_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32_UINT:return DXGI_FORMAT_R32_UINT;
    case VK_FORMAT_R32_SINT:return DXGI_FORMAT_R32_SINT;
    case VK_FORMAT_R8G8_UNORM:return DXGI_FORMAT_R8G8_UNORM;
    case VK_FORMAT_R8G8_UINT:return DXGI_FORMAT_R8G8_UINT;
    case VK_FORMAT_R8G8_SNORM:return DXGI_FORMAT_R8G8_SNORM;
    case VK_FORMAT_R8G8_SINT:return DXGI_FORMAT_R8G8_SINT;
    case VK_FORMAT_R16_SFLOAT:return DXGI_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_UINT:return DXGI_FORMAT_R16_UINT;
    case VK_FORMAT_R16_SNORM:return DXGI_FORMAT_R16_SNORM;
    case VK_FORMAT_R16_SINT:return DXGI_FORMAT_R16_SINT;
    case VK_FORMAT_R8_UNORM:return DXGI_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_UINT:return DXGI_FORMAT_R8_UINT;
    case VK_FORMAT_R8_SNORM:return DXGI_FORMAT_R8_SNORM;
    case VK_FORMAT_R8_SINT:return DXGI_FORMAT_R8_SINT;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:return DXGI_FORMAT_BC1_UNORM;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:return DXGI_FORMAT_BC1_UNORM_SRGB;
    case VK_FORMAT_BC2_UNORM_BLOCK:return DXGI_FORMAT_BC2_UNORM;
    case VK_FORMAT_BC2_SRGB_BLOCK:return DXGI_FORMAT_BC2_UNORM_SRGB;
    case VK_FORMAT_BC3_UNORM_BLOCK:return DXGI_FORMAT_BC3_UNORM;
    case VK_FORMAT_BC3_SRGB_BLOCK:return DXGI_FORMAT_BC3_UNORM_SRGB;
    case VK_FORMAT_BC4_UNORM_BLOCK:return DXGI_FORMAT_BC4_UNORM;
    case VK_FORMAT_BC4_SNORM_BLOCK:return DXGI_FORMAT_BC4_SNORM;
    case VK_FORMAT_BC5_UNORM_BLOCK:return DXGI_FORMAT_BC5_UNORM;
    case VK_FORMAT_BC5_SNORM_BLOCK:return DXGI_FORMAT_BC5_SNORM;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:return DXGI_FORMAT_BC6H_UF16;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:return DXGI_FORMAT_BC6H_SF16;
    case VK_FORMAT_BC7_UNORM_BLOCK:return DXGI_FORMAT_BC7_UNORM;
    case VK_FORMAT_BC7_SRGB_BLOCK:return DXGI_FORMAT_BC7_UNORM_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:return DXGI_FORMAT_BC1_UNORM;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:return DXGI_FORMAT_BC1_UNORM_SRGB;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}

inline bool binary(const std::filesystem::path& path,const void* data,uint64_t bytes) {
    std::ofstream out(path,std::ios::binary);out.write(static_cast<const char*>(data),std::streamsize(bytes));out.close();return bool(out);
}

inline bool dds(const std::filesystem::path& path,const Image& image,const void* data,uint64_t bytes,std::string& reason) {
    const auto format=dxgi(image.format);
    if(format==DXGI_FORMAT_UNKNOWN){reason="Native image format has no lossless DDS mapping";return false;}
    uint32_t bw=0,bh=0,block=0;
    if(!format_block(image.format,bw,bh,block) || !image.width || !image.height ||
       uint64_t((image.width+bw-1)/bw)*((image.height+bh-1)/bh)*block!=bytes){reason="DDS cell span is inconsistent";return false;}
    // DDS magic, 124-byte header and DX10 extension: one exact 2D view cell.
    uint32_t header[37]{};header[0]=0x20534444;header[1]=124;header[2]=0x1007|(bw==1?8:0x80000);
    header[3]=image.height;header[4]=image.width;header[5]=bw==1?image.width*block:uint32_t(bytes);header[7]=1;
    header[19]=32;header[20]=4;header[21]=0x30315844;header[27]=0x1000;
    header[32]=format;header[33]=3;header[35]=1;
    if(image.format==VK_FORMAT_BC1_RGB_UNORM_BLOCK || image.format==VK_FORMAT_BC1_RGB_SRGB_BLOCK)header[36]=3;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(header),sizeof(header));
    out.write(static_cast<const char*>(data),std::streamsize(bytes));out.close();
    if(!out){reason="DDS file write failed";return false;}return true;
}

inline bool jpeg(const std::filesystem::path& path,const Image& image,const void* data,uint64_t bytes,std::string& reason) {
    GUID source{};uint32_t pixel=0;
    switch(image.format){
    case VK_FORMAT_R8_UNORM:source=GUID_WICPixelFormat8bppGray;pixel=1;break;
    case VK_FORMAT_R16_UNORM:source=GUID_WICPixelFormat16bppGray;pixel=2;break;
    case VK_FORMAT_R16G16B16A16_UNORM:source=GUID_WICPixelFormat64bppRGBA;pixel=8;break;
    case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:source=GUID_WICPixelFormat32bppRGBA;pixel=4;break;
    case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:source=GUID_WICPixelFormat32bppBGRA;pixel=4;break;
    default:reason="Native image format requires a JPEG conversion adapter";return false;
    }
    if(!image.width || !image.height || uint64_t(image.width)*image.height*pixel!=bytes || bytes>UINT32_MAX){reason="JPEG cell span is inconsistent";return false;}
    struct Apartment { HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();} } apartment;
    HRESULT hr=apartment.hr==RPC_E_CHANGED_MODE?S_OK:apartment.hr;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;ComPtr<IWICBitmap> bitmap;ComPtr<IWICFormatConverter> converter;
    if(SUCCEEDED(hr))hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()));
    if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&properties);
    if(SUCCEEDED(hr))hr=frame->Initialize(properties.Get());
    if(SUCCEEDED(hr))hr=frame->SetSize(image.width,image.height);
    GUID target=GUID_WICPixelFormat24bppBGR;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&target);
    if(SUCCEEDED(hr))hr=factory->CreateBitmapFromMemory(image.width,image.height,source,image.width*pixel,UINT(bytes),static_cast<BYTE*>(const_cast<void*>(data)),&bitmap);
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
    if(SUCCEEDED(hr))hr=converter->Initialize(bitmap.Get(),target,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(SUCCEEDED(hr))hr=frame->WriteSource(converter.Get(),nullptr);
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    if(FAILED(hr)){reason="JPEG encoding failed: HRESULT "+std::to_string(uint32_t(hr));return false;}return true;
}

inline bool text(const std::filesystem::path& path,const void* data,uint64_t bytes,const std::string& metadata) {
    std::ofstream out(path,std::ios::binary);out.imbue(std::locale::classic());
    out<<"Native buffer words; byte offsets are relative to the captured span.\n"<<metadata<<"\noffset: hex uint float\n";
    const auto* source=static_cast<const uint8_t*>(data);uint64_t offset=0;
    for(;bytes-offset>=4;offset+=4){uint32_t word;float scalar;std::memcpy(&word,source+offset,4);std::memcpy(&scalar,source+offset,4);
        out<<std::dec<<offset<<": "<<std::hex<<std::setw(8)<<std::setfill('0')<<word<<std::dec<<' '<<word<<' '<<std::setprecision(9)<<scalar<<'\n';}
    for(;offset<bytes;++offset)out<<std::dec<<offset<<": byte "<<unsigned(source[offset])<<'\n';
    out.close();return bool(out);
}

inline std::string write(const std::filesystem::path& directory,const std::string& file,const void* data,uint64_t bytes,
                         const std::string& metadata,const Image& image,uint32_t options) {
    JsonWriter w;w.begin_array();
    auto artifact=[&](const char* kind,const std::filesystem::path& path,bool success,const std::string& reason={}){
        w.begin_object();w.kv("format",kind);w.kv("file",path.filename().u8string());w.kv("state",success?"complete":reason.empty()?"write_failed":"refused");
        if(!reason.empty())w.kv("reason",reason);w.end_object();
        if(!success){std::error_code error;std::filesystem::remove(path,error);}
    };
    auto path=[&](const char* extension){auto p=directory/file;p.replace_extension(extension);return p;};
    if(image.format!=VK_FORMAT_UNDEFINED){
        uint32_t formats=options&0x9c0u;if(!formats)formats=0x40u;
        bool automatic_failed=false;
        if(formats&0xc0u){std::string reason;const auto p=path(".jpg");const bool ok=jpeg(p,image,data,bytes,reason);
            automatic_failed=!ok;
            if(ok || (formats&0x80u))artifact("jpg",p,ok,reason);
            else {std::error_code error;std::filesystem::remove(p,error);}
        }
        if((formats&0x100u) || ((formats&0x40u)&&automatic_failed)){std::string reason;const auto p=path(".dds");const bool ok=dds(p,image,data,bytes,reason);artifact("dds",p,ok,reason);}
    }else{
        uint32_t formats=options&0xe00u;if(!formats && (options&0x38u))formats=0x400u;
        if(formats&0x200u){const auto p=path(".buf");artifact("buf",p,binary(p,data,bytes));}
        if(formats&0x400u){const auto p=path(".txt");artifact("txt",p,text(p,data,bytes,metadata));}
    }
    if(options&0x800u){const auto p=path(".dsc");artifact("desc",p,binary(p,metadata.data(),metadata.size()));}
    w.end_array();return w.out;
}
} // namespace efvk::analysis_export
