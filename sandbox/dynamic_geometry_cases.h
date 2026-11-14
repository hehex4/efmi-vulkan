static void dynamic_geometry_cases()
{
    CmdBuf cb;const VkBuffer first=h<VkBuffer>(91),second=h<VkBuffer>(92);
    const VkDeviceSize zero=0,size=64,stride=8,offset=16;
    on_bind_vertex_buffers(&cb,0,1,&first,&zero,&size,&zero);
    check(cb.vb[0].buf==first && cb.vb[0].size==64 && cb.vb[0].stride_known && cb.vb[0].stride==0,
          "an explicitly zero dynamic stride is known and retains its bound range");
    on_bind_vertex_buffers(&cb,0,1,&second,&offset);
    check(cb.vb[0].buf==second && cb.vb[0].off==16 && cb.vb[0].size==VK_WHOLE_SIZE && cb.vb[0].stride_known && !cb.vb[0].stride,
          "null-stride and legacy rebinding change buffer/range while preserving dynamic stride");
    on_bind_vertex_buffers(&cb,1,1,&first,&zero,&size,&stride);
    check(!cb.vb[0].stride && cb.vb[1].stride==8 && cb.vb[1].size==64,
          "nonzero first binding keeps other slots' independent dynamic state");
    const auto prior=cb.vb[0];on_bind_vertex_buffers(&cb,UINT32_MAX,UINT32_MAX,&first,&zero,&size,&stride);
    check(cb.vb[0].buf==prior.buf && cb.vb[0].size==prior.size && cb.vb[0].stride==prior.stride,
          "out-of-range binding arithmetic cannot wrap into tracked geometry slots");
    Device device;const auto handle=h<VkCommandBuffer>(93);auto& child=device.cbs[handle];child=std::make_unique<CmdBuf>();
    child->draws.emplace_back();child->draws.back().vb[0]=cb.vb[0];child->draws.back().topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    CmdBuf parent;on_execute_commands(&device,&parent,1,&handle);
    check(parent.draws.size()==1 && parent.draws[0].vb[0].buf==second && parent.draws[0].vb[0].stride_known &&
          !parent.draws[0].vb[0].stride && parent.draws[0].topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && !parent.vb[0].stride_known,
          "secondary draw snapshots preserve dynamic geometry without importing binding state");
    cb.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;cb.reset_recording();
    check(!cb.vb[0].stride_known && !cb.vb[1].stride_known && cb.vb[0].size==VK_WHOLE_SIZE && cb.topology==UINT32_MAX,
          "recording reset clears dynamic geometry validity and restores unbounded defaults");
    VkVertexInputBindingDescription2EXT binding{VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT};
    binding.binding=1;binding.stride=8;binding.inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;binding.divisor=0;
    VkVertexInputAttributeDescription2EXT attribute{VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT};
    attribute.binding=1;attribute.location=2;attribute.offset=4;attribute.format=VK_FORMAT_R32_SFLOAT;
    on_set_vertex_input(&cb,1,&binding,1,&attribute);
    const auto saved=cb.vertex_input;
    check(saved && saved->complete && saved->bindings[0].divisor==0 && saved->bindings[0].per_instance &&
          saved->attributes[0].location==2 && cb.vb[1].stride_known && cb.vb[1].stride==8,
          "dynamic declarations retain zero instance divisor and set independent binding stride");
    binding.stride=16;binding.divisor=2;attribute.offset=8;
    on_set_vertex_input(&cb,1,&binding,1,&attribute);
    check(saved->bindings[0].stride==8 && saved->attributes[0].offset==4 && cb.vertex_input->bindings[0].divisor==2,
          "a new setter and reused source arrays cannot mutate an earlier draw declaration");
    on_bind_vertex_buffers(&cb,1,1,&first,&zero,nullptr,&zero);
    check(cb.vb[1].stride_known && !cb.vb[1].stride && cb.vertex_input->bindings[0].stride==16,
          "vertex buffer stride overrides remain separate from full input declarations");
    child->draws[0].vertex_input=saved;CmdBuf second_parent;on_execute_commands(&device,&second_parent,1,&handle);
    check(second_parent.draws[0].vertex_input==saved && !second_parent.vertex_input,
          "secondary draws retain immutable input without importing the current declaration");
    attribute.pNext=h<void*>(1);on_set_vertex_input(&cb,1,&binding,1,&attribute);
    check(!cb.vertex_input->complete && !cb.vb[1].stride_known && saved->complete,
          "unknown input chains invalidate current metadata without corrupting older snapshots");
    on_set_vertex_input(&cb,257,h<const VkVertexInputBindingDescription2EXT*>(1),0,nullptr);
    check(!cb.vertex_input->complete,"description limits refuse before reading unbounded arrays");
    on_set_vertex_input(&cb,0,nullptr,0,nullptr);
    check(cb.vertex_input->complete && cb.vertex_input->attributes.empty() && cb.vertex_input->bindings.empty(),
          "empty dynamic input clears previous declarations without being confused with unset state");
    cb.reset_recording();check(!cb.vertex_input && saved->complete,"reset releases current input while retained draw metadata survives");
}
