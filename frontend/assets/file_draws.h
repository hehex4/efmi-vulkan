#pragma once
#include "file_plan.h"
#include <array>

namespace efmi::assets {
enum class BindingSource { Application, Null, File };
struct FileBinding {
    BindingSource source=BindingSource::Application;
    std::shared_ptr<const FileResource> resource;
    std::shared_ptr<const BufferUpload> buffer;
    std::shared_ptr<const ImageUpload> image;
};
struct FileDraw {
    uint32_t index_count=0,first_index=0,instance_count=1,first_instance=0;
    int32_t vertex_offset=0;
    std::vector<LayoutEdit> layout;
    std::array<FileBinding,32> vertex;
    FileBinding index;
    std::array<FileBinding,128> pixel;
};
struct FileDraws {
    bool skip_original=false;
    std::vector<FileDraw> draws;
};
// Preflight only; no GPU commands are emitted. Native boundaries must already
// have been resolved by their adapters. Captures inherited binding state and
// owners separately for every draw. Pending/refusal preserves the whole output.
// File VB/IB bounds are proved conservatively using whole-file index extrema.
// Application bindings still need the layer's original-state/layout validation.
FileReadiness request_file_draws(const PreparedFilePlan&,ResourceCache&,FileDraws&,std::string& reason);
}
