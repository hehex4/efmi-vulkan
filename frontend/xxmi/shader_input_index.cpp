#include "shader_input_index.h"
#include "shader_assembly.h"
#include <Windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"cabinet.lib")

namespace efmi::frontend {
namespace {
constexpr size_t budget=8u*1024*1024;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
std::array<uint8_t,32> digest(const void* data,size_t size){
    // A process/module-local provider; no environment or global OS state changes.
    struct Provider {
        BCRYPT_ALG_HANDLE handle=nullptr;
        Provider(){if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)handle=nullptr;}
        ~Provider(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
    };
    static const Provider provider;
    struct Hash { BCRYPT_HASH_HANDLE handle=nullptr; ~Hash(){if(handle)BCryptDestroyHash(handle);} } hash;
    require(provider.handle && size<=UINT32_MAX && BCryptCreateHash(provider.handle,&hash.handle,nullptr,0,nullptr,0,0)>=0,
            "Shader input SHA-256 creation failed");
    std::array<uint8_t,32> result{};
    require(BCryptHashData(hash.handle,static_cast<PUCHAR>(const_cast<void*>(data)),ULONG(size),0)>=0 &&
            BCryptFinishHash(hash.handle,result.data(),ULONG(result.size()),0)>=0,"Shader input SHA-256 failed");
    return result;
}
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data):data_(data){}
    const uint8_t* take(size_t size){require(size<=data_.size()-at_,"Truncated shader input cache");
        const auto* result=data_.data()+at_;at_+=size;return result;}
    uint32_t word(){uint32_t value;std::memcpy(&value,take(4),4);return value;}
    bool done() const {return at_==data_.size();}
    size_t position() const {return at_;}
private:
    const std::vector<uint8_t>& data_;size_t at_=0;
};
}
bool ShaderInputIndex::load(const std::filesystem::path& path,std::string& reason){
    try {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        require(bool(file),"Shader input cache is unavailable");
        const auto size=file.tellg();require(size>=48 && size<=budget,"Shader input cache size exceeds its budget");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
        require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)) && file.peek()==EOF,"Shader input cache changed while reading");
        return decode(bytes,reason);
    } catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderInputIndex::decode(const std::vector<uint8_t>& bytes,std::string& reason){
    try {
        require(bytes.size()>=48 && bytes.size()<=budget,"Shader input cache size exceeds its budget");
        Reader input(bytes);require(!std::memcmp(input.take(8),"EFVIS001",8),"Unknown shader input cache version");
        const uint32_t count=input.word(),size=input.word();
        require(count<=65536 && size==bytes.size()-48 && count<=size/40,"Invalid shader input cache framing");
        const auto expected=input.take(32);const auto actual=digest(bytes.data()+48,size);
        require(!std::memcmp(expected,actual.data(),32),"Shader input cache checksum mismatch");
        std::map<std::array<uint8_t,32>,Entry> next;
        for(uint32_t i=0;i<count;++i){
            std::array<uint8_t,32> key{};std::memcpy(key.data(),input.take(32),32);
            Entry entry;entry.bytes=input.word();const uint32_t names=input.word();
            require(entry.bytes>=20 && entry.bytes<=16u*1024*1024 && entry.bytes%4==0 && names<=32,"Invalid shader input entry");
            std::set<uint32_t> locations;std::set<std::pair<std::string,uint32_t>> semantics;
            for(uint32_t j=0;j<names;++j){
                VertexSemantic semantic;semantic.location=input.word();semantic.index=input.word();const uint32_t length=input.word();
                require(semantic.location<256 && semantic.index<256 && length>0 && length<=128,"Invalid shader input semantic");
                const auto* text=input.take(length);semantic.name.assign(reinterpret_cast<const char*>(text),length);
                for(unsigned char c:semantic.name)require(c>=33 && c<=126,"Non-ASCII shader input semantic");
                require(locations.insert(semantic.location).second && semantics.emplace(semantic.name,semantic.index).second,
                        "Duplicate shader input semantic");
                entry.semantics.push_back(std::move(semantic));
            }
            require(next.emplace(key,std::move(entry)).second,"Duplicate shader input identity");
        }
        require(input.done(),"Trailing shader input cache bytes");
        entries_=std::move(next);reason.clear();return true;
    } catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderInputIndex::lookup(const std::vector<uint32_t>& code,VertexProfile& profile,std::string& reason) const {
    try {
        require(code.size()>=5 && code.size()<=(1u<<22) && code[0]==0x07230203,"Invalid vertex bytecode identity");
        const auto found=entries_.find(digest(code.data(),code.size()*4));
        require(found!=entries_.end() && found->second.bytes==code.size()*4,"No verified input semantics for this exact shader");
        return profile.prepare(code,found->second.semantics,reason,false);
    } catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderModelIndex::load(const std::filesystem::path& path,std::string& reason){
    try{
        std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
        require(bool(file) && size>=48 && size<=budget,"Shader model cache is unavailable or exceeds its budget");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
        require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)) && file.peek()==EOF,"Shader model cache changed while reading");
        return decode(bytes,reason);
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderModelIndex::decode(const std::vector<uint8_t>& bytes,std::string& reason){
    try{
        require(bytes.size()>=48 && bytes.size()<=budget,"Shader model cache exceeds its budget");
        Reader input(bytes);require(!std::memcmp(input.take(8),"EFVSM001",8),"Unknown shader model cache version");
        const auto count=input.word(),size=input.word();
        require(count<=65536 && size==bytes.size()-48 && size==count*40,"Invalid shader model cache framing");
        const auto expected=input.take(32);const auto actual=digest(bytes.data()+48,size);
        require(!std::memcmp(expected,actual.data(),32),"Shader model cache checksum mismatch");
        std::map<std::array<uint8_t,32>,Entry> next;
        for(uint32_t i=0;i<count;++i){
            std::array<uint8_t,32> key{};std::memcpy(key.data(),input.take(32),32);
            Entry entry;entry.bytes=input.word();const auto token=input.word(),kind=token>>16,major=(token>>4)&15,minor=token&15;
            require(entry.bytes>=20 && entry.bytes<=16u*1024*1024 && entry.bytes%4==0 && kind<6 &&
                (major==4 || major==5) && minor<=1 && !(token&0xff00),"Invalid shader model entry");
            entry.model=std::string(1,"pvghdc"[kind])+"s_"+std::to_string(major)+"_"+std::to_string(minor);
            require(next.emplace(key,std::move(entry)).second,"Duplicate shader model identity");
        }
        require(input.done(),"Trailing shader model cache bytes");entries_=std::move(next);reason.clear();return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderModelIndex::lookup(const std::vector<uint32_t>& code,std::string& model) const{
    model.clear();
    try{
        if(code.size()<5 || code.size()>(1u<<22) || code[0]!=0x07230203)return false;
        const auto found=entries_.find(digest(code.data(),code.size()*4));
        if(found==entries_.end() || found->second.bytes!=code.size()*4)return false;
        model=found->second.model;return true;
    }catch(const std::exception&){return false;}
}
bool ShaderSourceIndex::load(const std::filesystem::path& path,std::string& reason){
    try{
        std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
        require(bool(file) && size>=56 && size<=256u*1024*1024,"Shader source cache is unavailable or exceeds its budget");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
        require(bool(file.read(reinterpret_cast<char*>(bytes.data()),size)) && file.peek()==EOF,"Shader source cache changed while reading");
        return decode(bytes,reason);
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderSourceIndex::decode(const std::vector<uint8_t>& bytes,std::string& reason){
    try{
        require(bytes.size()>=56 && bytes.size()<=256u*1024*1024,"Shader source cache exceeds its budget");
        Reader input(bytes);require(!std::memcmp(input.take(8),"EFVSB001",8),"Unknown shader source cache version");
        const auto count=input.word(),sources=input.word(),size=input.word(),codec=input.word();
        require(count<=65536 && sources<=count && codec==COMPRESS_ALGORITHM_XPRESS_HUFF && bool(count)==bool(sources) &&
                size==bytes.size()-56 && size>=size_t(count)*40+size_t(sources)*40,"Invalid shader source cache framing");
        const auto expected=input.take(32);const auto actual=digest(bytes.data()+56,size);
        require(!std::memcmp(expected,actual.data(),32),"Shader source cache checksum mismatch");
        struct Record {std::array<uint8_t,32> key;uint32_t bytes,source;};
        std::vector<Record> records;std::set<uint32_t> used;
        for(uint32_t i=0;i<count;++i){Record r;std::memcpy(r.key.data(),input.take(32),32);
            r.bytes=input.word();r.source=input.word();
            require(r.bytes>=20 && r.bytes<=16u*1024*1024 && r.bytes%4==0 && r.source<sources,"Invalid shader source identity");
            records.push_back(r);used.insert(r.source);
        }
        require(used.size()==sources,"Unreferenced original shader bytecode");
        std::vector<Blob> blobs;std::set<std::array<uint8_t,32>> identities;
        for(uint32_t i=0;i<sources;++i){Blob b;b.bytes=input.word();b.packed=input.word();
            require(b.bytes>=32 && b.bytes<=4u*1024*1024 && b.packed && b.packed<=4u*1024*1024+65536,
                    "Original shader source exceeds its budget");
            std::memcpy(b.digest.data(),input.take(32),32);b.offset=uint32_t(input.position());input.take(b.packed);
            require(identities.insert(b.digest).second,"Duplicate original shader bytecode");blobs.push_back(b);
        }
        require(input.done(),"Trailing shader source cache bytes");
        std::map<std::array<uint8_t,32>,Entry> next;
        for(const auto& r:records)require(next.emplace(r.key,Entry{r.bytes,blobs[r.source]}).second,"Duplicate shader source identity");
        auto archive=std::make_shared<const std::vector<uint8_t>>(bytes);
        entries_=std::move(next);archive_=std::move(archive);reason.clear();return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
bool ShaderSourceIndex::lookup(const std::vector<uint32_t>& code,std::shared_ptr<const SourceShaderCode>& source,std::string& reason) const{
    source.reset();reason.clear();try{
        require(code.size()>=5 && code.size()<=(1u<<22) && code[0]==0x07230203,"Invalid native shader source identity");
        const auto found=entries_.find(digest(code.data(),code.size()*4));
        require(found!=entries_.end() && found->second.bytes==code.size()*4,"No unambiguous original bytecode for this shader");
        unsigned stages=0;char stage=0;
        for(size_t i=5;i<code.size();){const auto count=code[i]>>16,op=code[i]&65535;
            require(count && count<=code.size()-i,"Malformed native shader instruction");
            if(op==15){require(count>=4 && code[i+1]<6,"Unsupported native shader entry");stage="vhdgpc"[code[i+1]];++stages;}
            i+=count;
        }
        require(stages==1,"Original bytecode needs one proven native stage");
        const auto& b=found->second.blob;
        struct Decoder {DECOMPRESSOR_HANDLE handle=nullptr;~Decoder(){if(handle)CloseDecompressor(handle);}} decoder;
        require(CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&decoder.handle),"Cannot open shader source decoder");
        std::vector<uint8_t> bytes(b.bytes);SIZE_T written=0;
        require(Decompress(decoder.handle,archive_->data()+b.offset,b.packed,bytes.data(),bytes.size(),&written) && written==bytes.size(),
                "Original shader source decompression failed");
        require(digest(bytes.data(),bytes.size())==b.digest,"Original shader bytecode checksum mismatch");
        auto result=SourceShaderCode::create(bytes.data(),bytes.size());
        require(result->model().front()==stage,"Original bytecode stage conflicts with the native executable");
        source=std::move(result);return true;
    }catch(const std::exception& e){reason=e.what();return false;}
}
}
