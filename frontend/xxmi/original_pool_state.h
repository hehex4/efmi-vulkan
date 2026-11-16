#pragma once
#include "CommandList.h"
#include "ResourceHash.h"
#include <tuple>

namespace efmi::frontend {
template<class Tuple> auto copy_state_fields(Tuple&& fields){
    return std::apply([](auto&... value){return std::make_tuple(value...);},fields);
}
template<class Fields,class Values,size_t... I> void restore_state_fields(Fields&& fields,Values& values,std::index_sequence<I...>) noexcept {
    (std::swap(std::get<I>(fields),std::get<I>(values)),...);
}
// The source header grants this adapter friendship only. Pool algorithms and
// object layout remain pinned; restoration swaps preallocated state, not objects.
struct OriginalPoolState {
    static auto fields(CustomResourcePool& p){return std::tie(p.index_type,p.lazy_initialization,p.element_type_switch_reset,
        p.allocate_slot_on_missing,p.expiration_timeout_frames,p.reset_expired_elements,p.read_refreshes_expiration,
        p.spatial_radius,p.source_pool,p.pool_size,p.last_replacement_index,p.last_expiration_run);}
    using Fields=std::tuple<PoolIndexType,bool,bool,bool,unsigned,bool,bool,uint32_t,CustomResourcePool*,size_t,size_t,unsigned>;
    Fields values;
    std::vector<PoolElement> elements;
    std::vector<PoolSlot> index_table;
    std::unique_ptr<FlatHashMap<uint32_t,size_t>> index_map;
    explicit OriginalPoolState(CustomResourcePool& p):values(copy_state_fields(fields(p))),elements(p.elements),index_table(p.index_table),
        index_map(p.index_map?std::make_unique<FlatHashMap<uint32_t,size_t>>(*p.index_map):nullptr){}
    void restore(CustomResourcePool& p) noexcept {
        restore_state_fields(fields(p),values,std::make_index_sequence<std::tuple_size_v<Fields>>{});
        p.elements.swap(elements);p.index_table.swap(index_table);p.index_map.swap(index_map);
    }
    static CustomResourcePool* source(CustomResourcePool& p){return p.source_pool;}
    static size_t capacity(CustomResourcePool& p){return p.elements.size();}
    static CustomResourcePool* failed(PoolCopyOperation& p){return p.failed_root;}
    static void failed(PoolCopyOperation& p,CustomResourcePool* value) noexcept {p.failed_root=value;}
};
inline auto original_resource_fields(CustomResource& r){
    return std::tie(r.name,r.filename,r.pool,r.pool_index,r.max_copies_per_frame,r.substantiated,
        r.override_type,r.override_bind_flags,r.override_misc_flags,r.override_format,r.override_color_space,
        r.override_width,r.override_height,r.override_depth,r.override_mips,r.override_array,r.override_msaa,r.override_msaa_quality,
        r.override_byte_width,r.override_stride,r.width_multiply,r.height_multiply,r.initial_data,r.initial_data_size);
}
struct OriginalResourceMetadata {
    using Fields=decltype(copy_state_fields(original_resource_fields(std::declval<CustomResource&>())));
    Fields values;
    explicit OriginalResourceMetadata(CustomResource& r):values(copy_state_fields(original_resource_fields(r))){}
    void restore(CustomResource& r) noexcept {
        restore_state_fields(original_resource_fields(r),values,std::make_index_sequence<std::tuple_size_v<Fields>>{});
    }
};
}
