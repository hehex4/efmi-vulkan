#include "shader_input_index.h"
#include <Windows.h>
#include <bcrypt.h>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")

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
}
