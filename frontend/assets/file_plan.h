#pragma once
#include "resource_cache.h"
#include <cstdint>
namespace efmi::core {struct PaletteSnapshot;}

namespace efmi::assets {
enum class FileOperation { BindVertex, BindIndex, BindPixelResource, DrawIndexed, External };

// Owned frontend data. The byte uploader does not infer stride/index format
// from a filename. Neither this data nor its loader executes native Core calls.
struct FileResource {
    std::wstring name;
    std::filesystem::path path;
    VkBufferUsageFlags usage=0; // Zero means a DDS image.
    uint32_t stride=0;
    VkIndexType index_type=VK_INDEX_TYPE_MAX_ENUM;
};
struct LayoutEdit {
    struct Match {
        std::string semantic_name;
        uint32_t semantic_index=UINT32_MAX,input_slot=UINT32_MAX,format=0;
        uint32_t format_byte_size=UINT32_MAX,aligned_byte_offset=UINT32_MAX;
    } match;
    struct Replace {
        std::string semantic_name;
        uint32_t semantic_index=UINT32_MAX,format=0,aligned_byte_offset=UINT32_MAX;
    } replace;
};
struct FilePlanItem {
    FileOperation operation=FileOperation::External;
    uint32_t slot=0;
    std::shared_ptr<const FileResource> resource; // Null is an explicit unbind.
    int32_t application_slot=-1; // the application's texture at this DX11 pixel slot of the draw (PlanItem)
    uint32_t index_count=0,first_index=0,instance_count=1,first_instance=0;
    int32_t vertex_offset=0;
    std::vector<LayoutEdit> layout;
    std::wstring external;
    uint32_t phase_mask=0;
    std::shared_ptr<const core::PaletteSnapshot> palette;
};
struct FilePlan {
    bool skip_original=false;
    std::vector<FilePlanItem> items;
    size_t pre_items=SIZE_MAX; // items before the original draw (DrawPlan::pre_items)
};
enum class FileReadiness { Pending, Ready, Failed };
class PreparedFilePlan {
public:
    const FilePlan& plan() const {return plan_;}
    const std::vector<ResourceHandle>& requests() const {return requests_;}
private:
    friend bool prepare_file_plan(const FilePlan&,ResourceCache&,PreparedFilePlan&,std::string&);
    friend FileReadiness request_file_plan(const PreparedFilePlan&,ResourceCache&,std::vector<ResourceSnapshot>&,std::string&);
    FilePlan plan_;
    // One slot per operation. Only nonnull file bindings have requests.
    std::vector<ResourceHandle> requests_;
};

// Setup only: resolves cache handles with no file IO or demand. Output remains
// unchanged on refusal; earlier declarations may remain dormant in the cache.
bool prepare_file_plan(const FilePlan&,ResourceCache&,PreparedFilePlan&,std::string& reason);
// Demand and collect immutable Ready owners. Pending/Failed preserves output.
// A Ready result means assets only: External items still require their native
// adapters before the complete draw transaction may execute.
FileReadiness request_file_plan(const PreparedFilePlan&,ResourceCache&,
                               std::vector<ResourceSnapshot>&,std::string& reason);
}
