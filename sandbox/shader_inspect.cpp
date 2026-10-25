// Compile one original source declaration without a GPU or installed-file writes.
#include "../frontend/gpu/hlsl_compiler.h"
#include "../frontend/gpu/dxbc_compiler.h"
#include <fstream>
#include <cstdio>
#include <climits>
#include <stdexcept>
using namespace efmi::gpu;
static void json(std::ostream& out,const std::wstring& value){out<<'"';for(wchar_t c:value){
    if(c==L'"' || c==L'\\')out<<'\\'<<char(c);else if(c<32 || c>126){char text[7];snprintf(text,sizeof(text),"\\u%04x",unsigned(c));out<<text;}else out<<char(c);}out<<'"';}
static void json(std::ostream& out,const std::string& value){std::wstring bytes;for(unsigned char c:value)bytes.push_back(c);json(out,bytes);}
static void binary(const std::filesystem::path& path,const void* data,size_t size){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(static_cast<const char*>(data),size);if(!file)throw std::runtime_error("Cannot write compiler evidence");}
static unsigned number(const wchar_t* text){wchar_t* end=nullptr;const auto value=wcstoul(text,&end,10);if(!*text || *end || value>UINT_MAX)throw std::runtime_error("Invalid compiler numeric argument");return unsigned(value);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=9 && argc!=10)throw std::runtime_error("Expected source, working directory, flags, stage, include policy, Vulkan target, compiler DLL, output directory and optionally the bytecode translator DLL");
    HlslRequest request;request.source=argv[1];request.working_directory=argv[2];request.flags=number(argv[3]);
    if(wcslen(argv[4])!=1 || !wcschr(L"vhdgpc",argv[4][0]))throw std::runtime_error("Invalid original shader stage");request.stage=char(argv[4][0]);
    wchar_t* end=nullptr;const auto recursive=wcstol(argv[5],&end,10);if(!*argv[5] || *end)throw std::runtime_error("Invalid include policy");request.recursive_include=int(recursive);
    if(!wcscmp(argv[6],L"1.2"))request.vulkan_version=VK_API_VERSION_1_2;else if(!wcscmp(argv[6],L"1.1"))request.vulkan_version=VK_API_VERSION_1_1;else throw std::runtime_error("Invalid Vulkan target");
    const std::filesystem::path output=argv[8];std::filesystem::create_directories(output);
    HlslCompiler compiler(argv[7]);HlslProgram program;std::string why;std::vector<uint8_t> reference;
    bool passed=compiler.compile(request,program,why,argc==10?&reference:nullptr);bool translated=false;
    // As the runtime: a source DXC refuses and the original compiler takes runs as that bytecode's translation.
    if(!passed && !reference.empty()){DxbcCompiler bytecode(argv[9]);std::string translation;
        translated=passed=bytecode.compile(reference,request.stage,program,translation);if(passed)program.diagnostics=why;else why+="; its bytecode translation: "+translation;}
    std::ofstream report(output/"compiler.json",std::ios::binary|std::ios::trunc);report<<"{\"compiled\":"<<(passed?"true":"false")<<",\"bytecode_translation\":"<<(translated?"true":"false")<<",\"diagnostics\":";json(report,passed?program.diagnostics:why);
    report<<",\"dependencies\":[";
    if(passed){binary(output/"module.spv",program.spirv.data(),program.spirv.size()*4);binary(output/"reference.dxbc",program.dxbc.data(),program.dxbc.size());
        for(size_t i=0;i<program.dependencies.size();++i){const auto& dependency=program.dependencies[i];const auto filename="dependency-"+std::to_string(i)+".bin";
            binary(output/filename,dependency.bytes.data(),dependency.bytes.size());if(i)report<<',';report<<"{\"path\":";json(report,dependency.path.wstring());report<<",\"snapshot\":";json(report,filename);report<<"}";}
    }
    report<<"],\"capabilities\":[";for(size_t i=0;i<program.capabilities.size();++i){if(i)report<<',';report<<program.capabilities[i];}
    report<<"],\"bindings\":[";for(size_t i=0;i<program.bindings.size();++i){if(i)report<<',';const auto& b=program.bindings[i];report<<"{\"name\":";json(report,b.name);
        report<<",\"source_type\":\""<<b.source_type<<"\",\"slot\":"<<b.slot<<",\"count\":"<<b.count<<",\"binding\":"<<b.binding<<",\"type\":"<<b.type<<",\"stride\":"<<b.stride<<"}";}
    report<<"],\"words\":"<<program.spirv.size()<<",\"reference_bytes\":"<<program.dxbc.size()<<"}\n";report.close();if(!report)throw std::runtime_error("Cannot write compiler report");
    printf("source stage %c: %s\n",request.stage,passed?"compiled":"refused");return passed?0:1;
}catch(const std::exception& e){fprintf(stderr,"Shader inspection failed: %s\n",e.what());return 2;}}
