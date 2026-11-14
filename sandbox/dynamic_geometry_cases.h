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
}
