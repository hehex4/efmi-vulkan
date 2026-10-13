// Standalone CPU fixture. Capture replay bytes never become production assets.
#include "../frontend/core/palette_bank.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
using namespace efmi::core;
using namespace efvk;
template<class T> static T get(const uint8_t* p){T v;std::memcpy(&v,p,sizeof(v));return v;}
template<class T> static void put(uint8_t* p,T v){std::memcpy(p,&v,sizeof(v));}
static InstanceEntry entry(uint32_t a=2000,uint32_t b=4000)
{
    InstanceEntry e;for(size_t i=0;i<e.size();++i)e[i]=uint8_t(i^0x37);
    put(e.data()+80,a);put(e.data()+84,b);return e;
}
static std::vector<uint8_t> source(float base)
{
    std::vector<uint8_t> data(4*48);
    for(size_t i=0;i<data.size()/4;++i)put(data.data()+4*i,base+float(i));
    return data;
}
static BoneWindow view(const std::vector<uint8_t>& v){return {v.data(),v.size()};}
static float value(const PaletteSnapshot& s,uint32_t pose,uint32_t bone)
{
    return get<float>(s.bones.data()+(512+pose*3+bone)*48);
}
static int tests()
{
    int passed=0,failed=0;auto check=[&](bool ok,const char* why){if(ok)++passed;else{++failed;printf("FAIL: %s\n",why);}};
    PaletteBank bank;std::string reason;std::shared_ptr<const PaletteSnapshot> out;
    auto current=source(10),previous=source(100),other=source(1000);const auto e=entry();
    PaletteDefinition def;def.bones=3;def.instances=2;def.components.resize(3);
    // Original component ids are deliberately unordered; slot 1 provides no bones.
    def.components[0].range={2,1};def.components[2].range={0,2};
    for(auto id:{0,2})for(auto& lod:def.components[id].lods)lod.available=true;
    def.components[2].lods[1].indices={2,0};
    check(!bank.begin_update(1,reason) && !bank.snapshot(1,e,out,reason),"unconfigured bank refuses use");
    check(bank.configure(def,reason) && bank.begin_update(0,reason),"unordered sparse ids configure with logical serial zero");
    auto import=[&](uint64_t token,unsigned c,unsigned lod,const InstanceEntry& item,BoneWindow a,BoneWindow b){
        return bank.import_component(token,c,lod,item,a,b,1,reason);
    };
    check(import(11,2,1,e,view(current),view(previous))==BoneImportResult::Imported && bank.live()==1,
          "first component creates one independent identity master");
    check(!bank.snapshot(11,e,out,reason) && !out,"cross-component mesh refuses until the remaining source bones arrive");
    check(import(11,0,0,e,view(current),view(previous))==BoneImportResult::Imported &&
          bank.snapshot(11,e,out,reason),"last source component completes the whole palette");
    check(out && out->bones.size()==(512+2*3)*48 && value(*out,0,0)==46 && value(*out,0,1)==22 &&
          value(*out,1,0)==136 && value(*out,0,2)==22,"remap directions, sparse ids and both poses share one compact skeleton");
    auto frozen=out;
    bool preserved=true;for(size_t i=0;i<e.size();++i)if(i<80 || i>=88)preserved&=out->entry[i]==e[i];
    check(preserved && get<uint32_t>(out->entry.data()+80)==1533 && get<uint32_t>(out->entry.data()+84)==1542,
          "per-identity entry patches only the two offsets using the measured addressing convention");
    check(import(11,2,0,e,view(other),view(previous))==BoneImportResult::Imported &&
          bank.snapshot(11,e,out,reason) && value(*out,0,0)==1012 && value(*frozen,0,0)==46,
          "better LOD supersedes the master while an already emitted snapshot stays immutable");
    check(import(11,2,1,e,{}, {})==BoneImportResult::AlreadyCurrent,"a later LOD1 cannot downgrade LOD0 in the same update");
    check(bank.begin_update(1,reason) && bank.snapshot(11,e,out,reason) && value(*out,0,0)==1012 && out->update==1,
          "the prior complete master remains available before this update's components arrive");
    const auto stable=out->bones;auto bad=previous;
    put(bad.data()+48+44,std::numeric_limits<float>::quiet_NaN());
    check(import(11,2,0,e,view(current),view(bad))==BoneImportResult::Rejected &&
          bank.snapshot(11,e,out,reason) && out->bones==stable,"bad previous pose cannot partly mutate the current palette");
    check(import(11,2,0,e,view(current),view(previous))==BoneImportResult::Imported &&
          bank.snapshot(11,e,out,reason) && value(*out,0,0)==22,"valid retry after failed import still updates the palette");
    check(import(22,0,0,e,view(other),view(other))==BoneImportResult::Imported &&
          !bank.snapshot(22,e,out,reason) && out->identity==11,"new instance cannot inherit another instance's completeness");
    check(import(22,2,0,e,view(other),view(other))==BoneImportResult::Imported &&
          bank.snapshot(22,e,out,reason) && value(*out,0,0)==1012,"second instance completes with independent bytes");
    auto second=out;
    check(import(33,0,0,e,view(current),view(previous))==BoneImportResult::Rejected && bank.live()==2,
          "instance budget refuses a third live identity");
    bank.retire(11);
    check(import(33,0,0,e,view(current),view(previous))==BoneImportResult::Imported &&
          !bank.snapshot(33,e,out,reason) && out==second && value(*frozen,0,0)==46,
          "retirement frees capacity but a reused slot needs fresh completeness; old snapshots stay alive");
    check(import(0,0,0,e,view(current),view(previous))==BoneImportResult::Rejected &&
          import(33,1,0,e,view(current),view(previous))==BoneImportResult::Rejected,"zero identity and empty component cannot import");
    auto invalid=def;invalid.components[2].range.count=1;
    check(!bank.configure(invalid,reason) && bank.live()==2 && bank.snapshot(22,e,out,reason) &&
          out->bones==second->bones,"failed configuration preserves existing masters");
    check(!bank.begin_update(0,reason) && bank.snapshot(22,e,out,reason) && out->update==1,
          "backwards update does not change current state");
    invalid=def;invalid.instances=64;invalid.bones=65536;
    check(!bank.configure(invalid,reason) && bank.live()==2,"full per-identity CPU storage is bounded before allocation");
    invalid=def;invalid.components[2].lods[1].indices={0};
    check(!bank.configure(invalid,reason),"remap must provide every destination bone");
    def.components[0].lods[1].available=false;
    check(bank.configure(def,reason) && bank.live()==0 && bank.begin_update(3,reason) &&
          import(1,0,1,e,view(current),view(previous))==BoneImportResult::Rejected && bank.live()==0,
          "unassigned LOD cannot create an identity");
    auto only_current=entry(2000,0);
    check(import(1,0,0,only_current,view(current),{})==BoneImportResult::Imported &&
          import(1,2,0,only_current,view(current),{})==BoneImportResult::Imported &&
          bank.snapshot(1,only_current,out,reason) && !bank.snapshot(1,e,out,reason),
          "completeness is tracked separately for current and previous dynamic poses");
    check(bank.begin_update(4,reason) &&
          import(1,0,0,e,view(current),view(previous))==BoneImportResult::Imported &&
          !bank.snapshot(1,e,out,reason) &&
          import(1,2,0,e,view(current),view(previous))==BoneImportResult::Imported && bank.snapshot(1,e,out,reason),
          "both poses become usable only after every source component supplies them");
    printf("Shared character palettes: %d checks passed, %d failed\n",passed,failed);return failed?1:0;
}
class Reader {
    std::vector<uint8_t> data;size_t at=0;
public:
    explicit Reader(const char* path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);auto size=in.tellg();
        if(!in || size<0 || size>(64<<20))throw std::runtime_error("Replay input budget");
        data.resize(size_t(size));in.seekg(0);in.read(reinterpret_cast<char*>(data.data()),size);
        if(!in)throw std::runtime_error("Replay read failed");
    }
    const uint8_t* take(size_t n){
        if(n>data.size()-at)throw std::runtime_error("Truncated replay");
        const auto* p=data.data()+at;at+=n;return p;
    }
    template<class T>T scalar(){return get<T>(take(sizeof(T)));}
    bool done()const{return at==data.size();}
};
template<class T>static void write(std::ofstream& out,const T& v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));}
static int replay(const char* input,const char* output)
{
    Reader reader(input);if(std::memcmp(reader.take(8),"EFMIPB01",8))throw std::runtime_error("Replay magic");
    PaletteDefinition def;def.bones=reader.scalar<uint32_t>();def.instances=reader.scalar<uint32_t>();
    auto count=reader.scalar<uint32_t>(),records=reader.scalar<uint32_t>();
    if(count>256 || records>4096)throw std::runtime_error("Replay count budget");
    def.components.resize(count);
    for(auto& c:def.components){
        c.range.first=reader.scalar<uint32_t>();c.range.count=reader.scalar<uint32_t>();
        for(auto& lod:c.lods){
            lod.available=reader.scalar<uint32_t>()!=0;auto n=reader.scalar<uint32_t>();
            if(n>65536)throw std::runtime_error("Replay remap budget");
            lod.indices.resize(n);std::memcpy(lod.indices.data(),reader.take(n*2),n*2);
        }
    }
    PaletteBank bank;std::string reason;
    if(!bank.configure(std::move(def),reason))throw std::runtime_error(reason);
    std::ofstream out(output,std::ios::binary);out.write("EFMIPO01",8);write(out,records);
    size_t bytes=12;
    for(uint32_t i=0;i<records;++i){
        const auto serial=reader.scalar<uint64_t>(),identity=reader.scalar<uint64_t>();
        const auto component=reader.scalar<uint32_t>(),lod=reader.scalar<uint32_t>();auto scale=reader.scalar<float>();
        InstanceEntry entry;std::memcpy(entry.data(),reader.take(entry.size()),entry.size());
        BoneWindow views[2];for(auto& view:views){view.size=reader.scalar<uint32_t>();view.data=reader.take(view.size);}
        if(!bank.begin_update(serial,reason))throw std::runtime_error(reason);
        const auto imported=bank.import_component(identity,component,lod,entry,views[0],views[1],scale,reason);
        if(imported==BoneImportResult::Rejected)throw std::runtime_error(reason);
        std::shared_ptr<const PaletteSnapshot> snapshot;uint32_t ready=bank.snapshot(identity,entry,snapshot,reason)?1:0;
        write(out,uint32_t(imported));write(out,ready);bytes+=8;
        if(ready){
            bytes+=256+4+snapshot->bones.size();if(bytes>(64<<20))throw std::runtime_error("Replay output budget");
            out.write(reinterpret_cast<const char*>(snapshot->entry.data()),256);write(out,uint32_t(snapshot->bones.size()));
            out.write(reinterpret_cast<const char*>(snapshot->bones.data()),snapshot->bones.size());
        }
    }
    if(!reader.done() || !out)throw std::runtime_error("Replay trailing bytes or output failure");
    return 0;
}
int main(int argc,char** argv)
{
    try{
        if(argc==1)return tests();
        if(argc==4 && std::string(argv[1])=="--replay")return replay(argv[2],argv[3]);
        throw std::runtime_error("Invalid palette test arguments");
    }catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}
}
