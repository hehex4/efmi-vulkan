#include "shader_assembly.h"
#include "shader_state.h"
#include "util.h"
#include <mutex>
#include <stdexcept>

namespace efmi::frontend {
namespace {
void require_assembly(bool value,const char* text){if(!value)throw std::runtime_error(text);}
constexpr size_t max_code=4u<<20,max_text=16u<<20;
// The original assembler fills its opcode tables through operator[] (Assembler.cpp hackMap, insMap,
// ldMap, codeBin): one assembly at a time across generations and threads (D-129).
std::mutex assembler_mutex;
}
std::shared_ptr<const SourceShaderCode> SourceShaderCode::create(const void* bytes,size_t size){
    require_assembly(bytes && size<=max_code,"Original shader assembly bytecode exceeds budget");
    auto result=std::shared_ptr<SourceShaderCode>(new SourceShaderCode);
    result->model_=source_shader_model(bytes,size);
    result->declarations_=source_shader_declarations(bytes,size);
    const auto* data=static_cast<const uint8_t*>(bytes);result->bytes_.assign(data,data+size);
    // HackerDevice.cpp hash_shader: the pinned FNV helper starts at zero and
    // multiplies before XOR. BYTECODE hashes whitelisted payloads in table order.
    result->hashes_[0]=fnv_64_buf(bytes,size);
    for(unsigned i=0;i<8;++i)result->hashes_[1]=(result->hashes_[1]<<8)|data[4+i];
    auto word=[&](size_t offset){uint32_t value;memcpy(&value,data+offset,4);return value;};
    uint32_t hash=0;
    for(uint32_t i=0;i<word(28);++i){const size_t at=word(32+size_t(i)*4);
        for(const char* tag:{"SHDR","SHEX","ISGN","ISG1","PCSG","PSG1","OSGN","OSG5","OSG1"})
            if(!memcmp(data+at,tag,4))hash=crc32c_hw(hash,data+at+8,word(at+4));
    }
    result->hashes_[2]=hash?uint64_t(hash):result->hashes_[0];return result;
}
std::string source_shader_disassembly(const SourceShaderCode& code,bool patch_cb_offsets,bool custom_data){
    auto text=BinaryToAsmText(code.bytes().data(),code.bytes().size(),patch_cb_offsets,custom_data);
    require_assembly(!text.empty() && text.size()<=max_text,"Original shader disassembly failed or exceeds budget");return text;
}
std::vector<uint8_t> source_shader_assembly(const std::string& text,std::vector<std::string>& notices){
    require_assembly(!text.empty() && text.size()<=max_text,"Original shader assembly text exceeds budget");
    std::vector<char> input(text.begin(),text.end());std::vector<byte> output;std::vector<AssemblerParseError> errors;
    HRESULT result;
    {std::lock_guard<std::mutex> lock(assembler_mutex);result=AssembleFluganWithSignatureParsing(&input,&output,&errors);}
    require_assembly(SUCCEEDED(result) && !output.empty() && output.size()<=max_code,"Original signature-aware shader assembly failed");
    source_shader_model(output.data(),output.size());source_shader_declarations(output.data(),output.size());
    require_assembly(errors.size()<=4096,"Original assembly diagnostic budget exceeded");
    std::vector<std::string> messages;size_t size=0;
    for(const auto& error:errors){messages.emplace_back(error.what());size+=messages.back().size();
        require_assembly(size<=1u<<20,"Original assembly diagnostic text exceeds budget");}
    notices=std::move(messages);return {output.begin(),output.end()};
}
}
