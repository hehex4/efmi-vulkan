// Failure rollback must isolate each call, even when two queues share a timeline.
#include "../layer/src/queue_signal_submission.h"
#include <condition_variable>
static void queue_signal_cases()
{
    const auto a=h<VkSemaphore>(9801),b=h<VkSemaphore>(9802),c=h<VkSemaphore>(9803);
    Device device;device.queue_signals=std::make_shared<QueueSignalHistory>();
    auto& history=*device.queue_signals;
    auto known=[&](VkSemaphore semaphore){return history.read().contains(semaphore);};
    VkSemaphore signals[]={a,b,a};
    VkSubmitInfo submits[2]={{VK_STRUCTURE_TYPE_SUBMIT_INFO},{VK_STRUCTURE_TYPE_SUBMIT_INFO}};
    submits[0].signalSemaphoreCount=2;submits[0].pSignalSemaphores=signals;
    submits[1].signalSemaphoreCount=1;submits[1].pSignalSemaphores=signals+2;
    bool forwarded=false,unlocked=false,visible=false;
    auto result=with_application_signals(&device,2,submits,[&]{
        forwarded=true;auto view=history.try_read();unlocked=view.owns_lock();visible=view.contains(a)&&view.contains(b);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    });
    check(forwarded&&unlocked&&visible,"legacy submission publishes all batches without holding a history lock over the driver");
    check(result==VK_ERROR_OUT_OF_HOST_MEMORY&&!known(a)&&!known(b),"failed legacy submission rolls back every new signal, including duplicates");
    VkSemaphoreSubmitInfo signal2{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};signal2.semaphore=b;signal2.value=19;
    VkSubmitInfo2 submit2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};submit2.signalSemaphoreInfoCount=1;submit2.pSignalSemaphoreInfos=&signal2;
    result=with_application_signals(&device,1,&submit2,[]{return VK_ERROR_OUT_OF_DEVICE_MEMORY;});
    check(result==VK_ERROR_OUT_OF_DEVICE_MEMORY&&!known(b),"failed sync2 submission rolls back its signal and preserves the result");
    with_application_signals(&device,1,&submit2,[]{return VK_SUCCESS;});
    check(known(b),"successful sync2 submission retains its producer history");
    result=with_application_signals(&device,2,submits,[]{return VK_ERROR_OUT_OF_HOST_MEMORY;});
    check(result==VK_ERROR_OUT_OF_HOST_MEMORY&&!known(a)&&known(b),"failure preserves prior committed history without retaining unrelated new signals");
    history.forget(a);history.forget(b);
    {
        QueueSignalHistory::Attempt first(history),second(history);
        first.signal(a);second.signal(a);
        first.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(known(a),"one failed queue attempt cannot erase another queue's pending contribution");
        second.finish(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        check(!known(a),"two failed overlapping attempts leave no producer history");
    }
    {
        QueueSignalHistory::Attempt first(history),second(history);
        first.signal(a);second.signal(a);second.finish(VK_ERROR_OUT_OF_DEVICE_MEMORY);first.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(!known(a),"reverse completion order cannot resurrect the other failed attempt");
    }
    {
        QueueSignalHistory::Attempt first(history),second(history);
        first.signal(a);second.signal(a);second.finish(VK_SUCCESS);first.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(known(a),"late failure retains a concurrent successful signal");
    }
    history.forget(a);
    {
        QueueSignalHistory::Attempt old(history);old.signal(a);history.forget(a);old.finish(VK_SUCCESS);
        check(!known(a),"late success cannot resurrect a forgotten semaphore identity");
    }
    {
        QueueSignalHistory::Attempt old(history);old.signal(a);history.forget(a);
        QueueSignalHistory::Attempt fresh(history);fresh.signal(a);old.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(known(a),"old failure cannot erase a new object using the same raw handle");
        fresh.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(!known(a),"new object's failed attempt does not inherit an old contribution");
    }
    {
        QueueSignalHistory::Attempt old(history);old.signal(a);history.forget(a);
        QueueSignalHistory::Attempt fresh(history);fresh.signal(a);old.finish(VK_SUCCESS);fresh.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        check(!known(a),"old success cannot commit a new object's failed signal");
    }
    {
        QueueSignalHistory live;
        QueueSignalHistory::Attempt successful(live);
        for(uintptr_t i=0;i<65537;++i)successful.signal(h<VkSemaphore>(100000+i));
        successful.finish(VK_SUCCESS);
        QueueSignalHistory::Attempt failed(live);failed.signal(c);failed.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        auto view=live.read();
        check(view.contains(h<VkSemaphore>(100000))&&view.contains(h<VkSemaphore>(165536))&&!view.contains(c),
              "crossing the former capacity limit preserves other live objects even when the next submission fails");
    }
    {
        QueueSignalHistory::Attempt dropped(history);dropped.signal(c);
    }
    check(!known(c),"an abandoned attempt removes only its own provisional signals");
    history.forget(c);
    {
        QueueSignalHistory::Attempt once(history);once.signal(c);once.finish(VK_SUCCESS);once.finish(VK_ERROR_DEVICE_LOST);
        check(known(c),"finishing an attempt twice does not undo its committed result");
    }
    result=with_application_signals(&device,0,static_cast<const VkSubmitInfo*>(nullptr),[]{return VK_ERROR_DEVICE_LOST;});
    check(result==VK_ERROR_DEVICE_LOST&&known(c),"empty submit forwards device loss without changing unrelated history");
    history.forget(a);
    // Two host threads return from their mocked driver calls in a controlled order.
    std::mutex order;std::condition_variable ready;int phase=0;bool peer_survived=false;
    std::thread first([&]{
        QueueSignalHistory::Attempt attempt(history);attempt.signal(a);
        {std::unique_lock<std::mutex> lock(order);phase=1;ready.notify_all();ready.wait(lock,[&]{return phase==2;});}
        attempt.finish(VK_ERROR_OUT_OF_HOST_MEMORY);
        {std::lock_guard<std::mutex> lock(order);phase=3;}ready.notify_all();
    });
    std::thread second([&]{
        {std::unique_lock<std::mutex> lock(order);ready.wait(lock,[&]{return phase==1;});}
        QueueSignalHistory::Attempt attempt(history);attempt.signal(a);
        {std::unique_lock<std::mutex> lock(order);phase=2;ready.notify_all();ready.wait(lock,[&]{return phase==3;});}
        peer_survived=known(a);attempt.finish(VK_SUCCESS);
    });
    first.join();second.join();
    check(peer_survived&&known(a),"concurrent host threads preserve the surviving producer without serializing driver calls");
}
