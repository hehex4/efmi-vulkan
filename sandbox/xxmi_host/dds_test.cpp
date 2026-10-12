#include "dds.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace efmi::assets;
static void put(std::vector<uint8_t>& bytes,size_t at,uint32_t value)
{
    for(size_t i=0;i<4;++i)bytes[at+i]=static_cast<uint8_t>(value>>(8*i));
}
static std::vector<uint8_t> fixture(uint32_t format=99)
{
    std::vector<uint8_t> data(148+80,0);
    put(data,0,0x20534444);put(data,4,124);put(data,8,0x21007);put(data,12,4);put(data,16,8);
    put(data,28,4);put(data,76,32);put(data,80,4);put(data,84,0x30315844);put(data,108,0x1000);
    put(data,128,format);put(data,132,3);put(data,140,1);
    for(size_t i=148;i<data.size();++i)data[i]=static_cast<uint8_t>(i);
    return data;
}
int HostDdsTests()
{
    int passed=0,failed=0;
    auto check=[&](bool ok,const char* message){
        if(ok){++passed;printf("PASS: %s\n",message);}else{++failed;printf("FAIL: %s\n",message);}
    };
    std::string reason;DdsImage image;
    auto data=fixture(),original=data;
    check(decode_dds(data.data(),data.size(),image,reason) && image.format==VK_FORMAT_BC7_SRGB_BLOCK &&
          image.levels.size()==4 && image.levels[0].bytes==32 && image.levels[1].offset==180 &&
          image.levels[3].width==1 && image.levels[3].height==1 && image.levels[3].bytes==16,
          "DX10 BC7 sRGB retains small mip block sizes and source offsets");
    check(data==original,"DDS parsing never transforms or rewrites payload bytes");
    auto linear=fixture(98);
    check(decode_dds(linear.data(),linear.size(),image,reason) && image.format==VK_FORMAT_BC7_UNORM_BLOCK,
          "linear BC7 is distinct from sRGB");
    auto classic=fixture();classic.erase(classic.begin()+128,classic.begin()+148);put(classic,84,0x55354342);
    check(decode_dds(classic.data(),classic.size(),image,reason) && image.format==VK_FORMAT_BC5_UNORM_BLOCK &&
          image.levels[0].offset==128,"legacy BC5U keeps its two-channel compressed payload");
    const DdsImage retained=image;
    auto rejected=[&](std::vector<uint8_t> invalid,const char* message){
        check(!decode_dds(invalid.data(),invalid.size(),image,reason) && !reason.empty() &&
              image.format==retained.format && image.levels[0].offset==retained.levels[0].offset,message);
    };
    auto invalid=fixture();invalid.pop_back();rejected(invalid,"truncated final mip preserves previous output");
    invalid=fixture();invalid.push_back(0);rejected(invalid,"extra layers or trailing data are refused");
    invalid=fixture(97);rejected(invalid,"typeless BC7 cannot silently pick a color space");
    invalid=fixture();put(invalid,140,2);rejected(invalid,"DX10 arrays are explicit unsupported inputs");
    invalid=fixture();put(invalid,136,4);rejected(invalid,"DX10 cube flag is rejected");
    invalid=fixture();put(invalid,132,4);rejected(invalid,"DX10 3D resources are rejected");
    invalid=fixture();put(invalid,16,0);rejected(invalid,"zero width is rejected");
    invalid=fixture();put(invalid,16,32768);rejected(invalid,"excess dimensions are rejected");
    invalid=fixture();put(invalid,28,20);rejected(invalid,"impossible mip count is rejected");
    invalid=fixture();put(invalid,8,0xA1007);put(invalid,20,31);rejected(invalid,"contradictory compressed linear size is rejected");
    invalid=fixture();put(invalid,144,2);rejected(invalid,"premultiplied-alpha interpretation is not guessed");
    printf("DDS asset metadata: %d checks passed, %d failed\n",passed,failed);
    return failed;
}

int HostDdsFile(const wchar_t* path)
{
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<0 || file.tellg()>(128ull<<20)){fprintf(stderr,"DDS file outside size budget\n");return 1;}
    std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
    DdsImage image;std::string reason;
    if(!file || !decode_dds(data.data(),data.size(),image,reason)){fprintf(stderr,"DDS refused: %s\n",reason.c_str());return 1;}
    printf("{\"format\":%d,\"width\":%u,\"height\":%u,\"levels\":[",int(image.format),image.width,image.height);
    for(size_t i=0;i<image.levels.size();++i){
        const auto& level=image.levels[i];if(i)putchar(',');
        printf("{\"width\":%u,\"height\":%u,\"offset\":%zu,\"bytes\":%zu}",level.width,level.height,level.offset,level.bytes);
    }
    printf("]}\n");return 0;
}
