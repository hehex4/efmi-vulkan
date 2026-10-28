// Compile the pinned original implementation unchanged, exposing its private
// descriptor matcher without duplicating native predicates or fabricating COM.
#include "ResourceHash.cpp"
#include "resource_match.h"
#include <stdexcept>

namespace efmi::frontend {
namespace {
void match_require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
}
void source_resource_matches(const SourceResourceMatch& source,ResourceCopyTargetType type,
                             CommandListState& state,TextureOverrideMatches& matches){
    match_require(G && matches.empty(),"Resource matching needs an active generation and empty output");
    if(!source.bound || (G->mTextureOverrideMap.empty() && G->mFuzzyTextureOverrides.empty()))return;
    const auto& identity=source.identity;
    match_require(identity.tracking!=SourceResourceIdentity::Tracking::Unknown,"Original resource tracking identity is unknown");
    if(identity.tracking==SourceResourceIdentity::Tracking::Untracked)return;
    auto full_hash=[&](){match_require(identity.hash_known,"Original whole-resource hash is unknown");return identity.hash;};
    bool region=false;
    if(G->track_region_hashes && (type==ResourceCopyTargetType::INDEX_BUFFER || type==ResourceCopyTargetType::VERTEX_BUFFER)){
        match_require(state.call_info && std::holds_alternative<D3D11_BUFFER_DESC>(source.description),
                      "Original region matching requires a buffer and draw context");
        const auto offset=type==ResourceCopyTargetType::INDEX_BUFFER?
            GetIndexBufferRegionOffset(source.format,state.call_info,source.offset):GetVertexBufferRegionOffset(source.stride,state.call_info,source.offset);
        const auto size=type==ResourceCopyTargetType::INDEX_BUFFER?
            GetIndexBufferRegionSize(source.format,state.call_info):GetVertexBufferRegionSize(source.stride,state.call_info);
        region=size!=0;
        if(region){if(auto* candidates=get_fuzzy_matches_by_draw_info(state.call_info)){
            uint32_t hash=0;std::wstring why;
            match_require(bool(identity.region_hash),"Original resource region hash provider is unavailable");
            if(!identity.region_hash(offset,size,hash,why))throw std::runtime_error(why.empty()?"Original resource region hash is unknown":
                std::string(why.begin(),why.end()));
            if(!hash)hash=full_hash();
            if(hash)find_texture_overrides_by_hash_from_fuzzy_matches(hash,candidates,&matches,state.call_info);
        }}
    }
    // Native command lookup excludes whole-resource hash0 and appends fuzzy
    // matches even when exact matches exist. The creation helper differs.
    if(!region && !G->mTextureOverrideMap.empty())if(const auto hash=full_hash())find_texture_override_for_hash(hash,&matches,state.call_info);
    if(!G->mFuzzyTextureOverrides.empty()){
        match_require(!std::holds_alternative<std::monostate>(source.description),"Original resource description is unknown");
        std::visit([&](const auto& description){using T=std::decay_t<decltype(description)>;
            if constexpr(!std::is_same_v<T,std::monostate>)find_texture_overrides_for_desc(&description,&matches,state.call_info);
        },source.description);
    }
}
void region_hash_matches(uint32_t hash,CommandListState& state,TextureOverrideMatches& matches){
    match_require(G!=nullptr && state.call_info,"Region matching needs an active generation and a draw");
    // With track_region_hashes (EFMI's d3dx.ini) the original's region path; a generation that did not read
    // it matched the draw by this hash through the hash map, and so does this.
    if(!G->track_region_hashes){find_texture_override_for_hash(hash,&matches,state.call_info);return;}
    if(auto* candidates=get_fuzzy_matches_by_draw_info(state.call_info))
        find_texture_overrides_by_hash_from_fuzzy_matches(hash,candidates,&matches,state.call_info);
}
void described_resource_matches(const D3D11_TEXTURE2D_DESC& description,CommandListState& state,TextureOverrideMatches& matches){
    match_require(G!=nullptr,"Resource matching needs an active generation");
    find_texture_overrides_for_desc(&description,&matches,state.call_info);
}
}
