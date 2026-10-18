// Original PNG resources -> owned Vulkan images, compared with the pinned DirectXTK WIC loader
// on a headless WARP device: the call CustomResource::LoadFromFile makes, with the same flags.
#include "WICTextureLoader.h"
static uint32_t png_crc(const uint8_t* p,size_t n){
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<n;++i){c^=p[i];for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
    return ~c;
}
static void png_be(std::vector<uint8_t>& out,uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(uint8_t(v>>s));}
static void png_chunk(std::vector<uint8_t>& out,const char* type,const std::vector<uint8_t>& data){
    png_be(out,uint32_t(data.size()));const size_t start=out.size();
    out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
    png_be(out,png_crc(out.data()+start,out.size()-start));
}
// A PNG with one stored (uncompressed) zlib stream, so every chunk is under the test's control.
static std::vector<uint8_t> png_file(uint32_t width,uint32_t height,uint8_t depth,uint8_t color,const std::vector<uint8_t>& rows,
                                     bool srgb,const std::vector<std::pair<const char*,std::vector<uint8_t>>>& extra={}){
    std::vector<uint8_t> out{137,80,78,71,13,10,26,10},header;
    png_be(header,width);png_be(header,height);header.insert(header.end(),{depth,color,0,0,0});
    png_chunk(out,"IHDR",header);
    if(srgb)png_chunk(out,"sRGB",{0});
    for(const auto& [type,data]:extra)png_chunk(out,type,data);
    std::vector<uint8_t> z{0x78,0x01};size_t at=0;
    do{const size_t n=std::min<size_t>(65535,rows.size()-at);const bool last=at+n==rows.size();
        z.insert(z.end(),{uint8_t(last),uint8_t(n),uint8_t(n>>8),uint8_t(~n),uint8_t(~n>>8)});
        z.insert(z.end(),rows.begin()+at,rows.begin()+at+n);at+=n;}while(at<rows.size());
    uint32_t a=1,b=0;for(auto c:rows){a=(a+c)%65521;b=(b+a)%65521;}
    png_be(z,b<<16|a);png_chunk(out,"IDAT",z);png_chunk(out,"IEND",{});
    return out;
}
// Rows with filter type 0 and deterministic samples.
static std::vector<uint8_t> png_rows(uint32_t width,uint32_t height,uint32_t bytes_per_pixel){
    std::vector<uint8_t> rows;
    for(uint32_t y=0;y<height;++y){rows.push_back(0);
        for(uint32_t i=0;i<width*bytes_per_pixel;++i)rows.push_back(uint8_t((y*71+i*29+13)&0xff));}
    return rows;
}
struct PngReference {DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT width=0,height=0,mips=0,array=0,bind=0,misc=0;std::vector<uint8_t> bytes;};
static PngReference png_native(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& file,DirectX::WIC_LOADER_FLAGS flags){
    using Microsoft::WRL::ComPtr;PngReference out;ComPtr<ID3D11Resource> resource;
    check(SUCCEEDED(DirectX::CreateWICTextureFromFileEx(device,file.c_str(),0,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0,flags,&resource,nullptr)),
          "Native DirectXTK WIC loader accepts the PNG fixture");
    ComPtr<ID3D11Texture2D> texture;check(SUCCEEDED(resource.As(&texture)),"Native WIC result is a 2D texture");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    out.format=desc.Format;out.width=desc.Width;out.height=desc.Height;out.mips=desc.MipLevels;out.array=desc.ArraySize;out.bind=desc.BindFlags;out.misc=desc.MiscFlags;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Native PNG readback texture");
    context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map native PNG readback");
    const size_t pixel=out.format==DXGI_FORMAT_R8_UNORM?1:out.format==DXGI_FORMAT_R16_UNORM?2:out.format==DXGI_FORMAT_R16G16B16A16_UNORM?8:4;
    const size_t row=size_t(out.width)*pixel;
    for(UINT y=0;y<out.height;++y){const auto* p=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch;out.bytes.insert(out.bytes.end(),p,p+row);}
    context->Unmap(staging.Get(),0);return out;
}
static void image_ini_png(Device& d,const std::filesystem::path& root,const std::filesystem::path& installed){
    using Microsoft::WRL::ComPtr;
    const auto library=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(library!=nullptr,"System D3D11 reference loader for PNG");
    const auto create=reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(library,"D3D11CreateDevice"));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    check(create && SUCCEEDED(create(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"Headless WARP device for PNG");
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    // WIC's PNG decoder yields 32bppBGRA for 8-bit RGBA and gray-alpha, 24bppBGR for RGB and
    // indexed formats for palettes; the pinned loader keeps BGRA and expands the others to RGBA.
    struct Case {const char* name;std::vector<uint8_t> file;const char* color_space;DirectX::WIC_LOADER_FLAGS flags;DXGI_FORMAT expected;};
    const std::vector<uint8_t> palette{255,0,0, 0,255,0, 0,0,255, 255,255,0};
    std::vector<uint8_t> indexed;for(uint32_t y=0;y<3;++y){indexed.push_back(0);for(uint32_t x=0;x<5;++x)indexed.push_back(uint8_t((x+y)%4));}
    std::vector<Case> cases={
        {"rgba8",png_file(5,3,8,6,png_rows(5,3,4),false),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_B8G8R8A8_UNORM},
        {"rgba8-srgb",png_file(5,3,8,6,png_rows(5,3,4),true),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
        {"rgba8-force",png_file(5,3,8,6,png_rows(5,3,4),false),"sRGB",DirectX::WIC_LOADER_FORCE_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
        {"rgba8-ignore",png_file(5,3,8,6,png_rows(5,3,4),true),"Linear",DirectX::WIC_LOADER_IGNORE_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM},
        {"rgb8",png_file(5,3,8,2,png_rows(5,3,3),true),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
        {"gray8",png_file(5,3,8,0,png_rows(5,3,1),true),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R8_UNORM},
        {"gray16",png_file(5,3,16,0,png_rows(5,3,2),false),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R16_UNORM},
        {"gray-alpha8",png_file(5,3,8,4,png_rows(5,3,2),false),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_B8G8R8A8_UNORM},
        {"rgba16",png_file(5,3,16,6,png_rows(5,3,8),false),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R16G16B16A16_UNORM},
        {"rgb16",png_file(5,3,16,2,png_rows(5,3,6),false),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R16G16B16A16_UNORM},
        {"palette",png_file(5,3,8,3,indexed,false,{{"PLTE",palette},{"tRNS",{255,64,0}}}),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_R8G8B8A8_UNORM},
    };
    // The image panel's own pictures: scripts/check_resource_commands.py copies them from the
    // first enabled installed Mod that ships one and verifies the originals stay unchanged.
    std::vector<std::filesystem::path> panel;
    for(const wchar_t* name:{L"k.png",L"panel_icon.png"})if(std::filesystem::is_regular_file(installed/name))panel.push_back(installed/name);
    for(const auto& source:panel){std::ifstream in(source,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
        check(bytes.size()>8,"Read installed panel PNG");cases.push_back({source.filename()==L"k.png"?"panel":"panel-icon",std::move(bytes),nullptr,DirectX::WIC_LOADER_DEFAULT,DXGI_FORMAT_UNKNOWN});}
    for(const auto& c:cases){const auto directory=root/c.name;std::filesystem::create_directories(directory/"Mods");
        const auto file=directory/"Mods/image.png";{std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(c.file.data()),c.file.size());check(bool(out),"Write PNG fixture");}
        const auto native=png_native(device.Get(),context.Get(),file,c.flags);
        if(c.expected!=DXGI_FORMAT_UNKNOWN && native.format!=c.expected)printf("PNG %s: native format %d, expected %d\n",c.name,int(native.format),int(c.expected));
        if(c.expected!=DXGI_FORMAT_UNKNOWN)check(native.format==c.expected,"Native WIC loader chooses the documented format for the fixture");
        check(native.mips==1 && native.array==1 && native.misc==0,"Native WIC loader without a context creates one mip and no misc flags");
        std::string ini="namespace = images\n[Constants]\nglobal $format = 0\nglobal $width = 0\nglobal $height = 0\nglobal $mips = 0\n"
                        "[ResourceImage]\nfilename = image.png\nbind_flags = shader_resource\n";
        if(c.color_space)ini+=std::string("color_space = ")+c.color_space+"\n";
        ini+="[Present]\nps-t0 = ref ResourceImage\n$format = ps-t0->format\n$width = ps-t0->width\n$height = ps-t0->height\n$mips = ps-t0->mips\n";
        ImageIniFixture f(d,directory,ini);f.present();
        f.variable("format",float(native.format));f.variable("width",float(native.width));f.variable("height",float(native.height));f.variable("mips",1);
        check(f.read(0)==native.bytes,"Owned PNG image bytes equal the native DirectXTK WIC texture");
        check(f.srv(0).image_view->range().levelCount==1,"Owned PNG view spans its single mip");
        f.release();
    }
    {   // A WIC container the owned decoder does not handle refuses instead of loading nothing.
        const auto directory=root/"bmp";std::filesystem::create_directories(directory/"Mods");
        const std::vector<uint8_t> bmp{'B','M',58,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,1,0,0,0,1,0,0,0,1,0,24,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0};
        {std::ofstream out(directory/"Mods/image.bmp",std::ios::binary);out.write(reinterpret_cast<const char*>(bmp.data()),bmp.size());}
        ImageIniFixture f(d,directory,"namespace = images\n[ResourceImage]\nfilename = image.bmp\nbind_flags = shader_resource\n[Present]\nps-t0 = ref ResourceImage\n");
        f.backend->begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        check(!f.ini->present({},1,0,f.why) && f.why.find("other than PNG")!=std::string::npos,"Non-PNG WIC image refuses with its reason");
        refuses([&]{f.backend->finish();},"Refused PNG load prevents submission of partial work");f.backend->discard();f.release();
    }
    printf("PNG resources: %zu fixtures compared with the native WIC loader, %zu installed panel images\n",cases.size(),panel.size());
    if(SUCCEEDED(apartment))CoUninitialize();
    context.Reset();device.Reset();FreeLibrary(library);
}
