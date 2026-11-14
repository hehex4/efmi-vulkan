#pragma once
#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>
namespace efvk {
// Recording-local boundaries that forbid private render-pass splitting.
struct ExecutionScopes {
    using Query=std::tuple<uint64_t,uint32_t,uint32_t>;
    std::vector<Query> queries;
    bool known=true, feedback=false;
    void reset(){queries.clear();known=true;feedback=false;}
    void query(uint64_t pool,uint32_t number,uint32_t index,bool begin){
        const Query key{pool,number,index};const auto found=std::find(queries.begin(),queries.end(),key);
        if(begin){if(found!=queries.end() || queries.size()>=64){known=false;return;}queries.push_back(key);}
        else if(found==queries.end())known=false;else queries.erase(found);
    }
    void transform(bool begin){if(feedback==begin)known=false;feedback=begin;}
    bool splittable()const{return known && queries.empty() && !feedback;}
};
}
