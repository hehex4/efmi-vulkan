// Included after asset_cache_test.cpp: actual private storage allocations on both upload paths.
static void mutable_image_tests(Gpu& g)
{
    const auto folder=std::filesystem::current_path()/L"mutable-image-fixtures";
    std::filesystem::create_directories(folder);std::mutex queue_mutex;
    for(bool staged:{false,true})for(uint32_t layers:{1u,2u,6u,12u}){
        if(layers==12 && !g.cube_arrays)continue;
        const bool cube=layers>=6;auto bytes=staged_fixture(4,3,17);
        bytes.resize(148);put(bytes,128,28);
        for(uint32_t mip=0,size=4;mip<3;++mip,size=std::max(1u,size/2))
            for(uint32_t i=0;i<size*size*4;++i)bytes.push_back(uint8_t(17+i*7+mip*19));
        const std::vector<uint8_t> first(bytes.begin()+148,bytes.end());
        put(bytes,136,cube?4:0);put(bytes,140,cube?layers/6:layers);
        for(uint32_t layer=1;layer<layers;++layer)for(auto value:first)bytes.push_back(uint8_t(value+layer*13));
        const auto path=folder/(std::to_wstring(layers)+L".dds");write_fixture(path,bytes);
        auto device=cache_device(g);device.readback=true;
        if(staged){device.dispatch=g.dispatch;device.upload_queue=g.queue;device.upload_family=g.family;device.upload_mutex=&queue_mutex;}
        ResourceBudgets budgets;ResourceCache cache(device,budgets);std::string reason;
        auto shared=cache.declare_image(path,reason,7,0,true);
        auto a=cache.declare_image(path,reason,7,0,true,true),b=cache.declare_image(path,reason,7,0,true,true);
        check(shared && a && b && a!=b && shared!=a && shared!=b,"writable images have private cache identity, separate from readonly sharing");
        check(cache.declare_image(path,reason,7,0,true)==shared,"immutable image identity survives writable declarations");
        check(!cache.demand(a,1) && a->snapshot().state==ResourceState::Dormant,"writable uploads cannot discard initialized mip levels");
        cache.demand(a);cache.demand(b);
        auto left=await_texture(cache,a,g,!staged),right=await_texture(cache,b,g,!staged);
        const bool ready=left.image && right.image && left.state==ResourceState::Ready && right.state==ResourceState::Ready;
        check(ready,staged?"mutable image arena uploads complete":"mutable image worker uploads complete");
        if(!ready){printf("mutable image reason: %s / %s\n",left.reason.c_str(),right.reason.c_str());continue;}
        check(left.image->image()!=right.image->image() && left.image->view()!=right.image->view(),"equal-file writable declarations own distinct actual images and views");
        check(left.image->layout()==VK_IMAGE_LAYOUT_GENERAL && left.image->storage_view() &&
              left.image->storage_view()!=left.image->view() && left.image->skipped_levels()==0 &&
              left.image->storage_view_type()==(layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D),
              "mutable publication retains full mips and a distinct storage view, including cube faces");
        const std::vector<uint8_t> expected(bytes.begin()+148,bytes.end());
        check(staged_readback(g,*left.image)==expected && staged_readback(g,*right.image)==expected,
              "every private image mip and layer matches its initial file on the GPU");
        check(cache.release(a) && left.image->image() && a->snapshot().state==ResourceState::Dormant,
              "retained command ownership survives forgetting a writable image");
        const auto fresh=cache.declare_image(path,reason,7,0,true,true);
        check(fresh!=a && fresh!=b && fresh!=shared,"same-file reload receives fresh mutable image identity");
        cache.stop();left={};right={};
        check(!budgets.resident->used() && !budgets.staging->used(),"mutable image allocation charges end with final completion owners");
    }
}
