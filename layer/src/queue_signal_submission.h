#pragma once
#include "state.h"
#include "queue_signal_history.h"

namespace efvk {
inline void record_application_signals(QueueSignalHistory::Attempt& attempt,const VkSubmitInfo& info){
    for(uint32_t i=0;i<info.signalSemaphoreCount;++i)attempt.signal(info.pSignalSemaphores[i]);
}
inline void record_application_signals(QueueSignalHistory::Attempt& attempt,const VkSubmitInfo2& info){
    for(uint32_t i=0;i<info.signalSemaphoreInfoCount;++i)attempt.signal(info.pSignalSemaphoreInfos[i].semaphore);
}
template<class Submit,class Call>
VkResult with_application_signals(Device* device,uint32_t count,const Submit* submits,Call call){
    // Other queues may enter while the driver is submitting this call. Preserve
    // provisional visibility, but roll back only this call on any error. This is
    // classification history, not proof of a particular binary payload or value.
    QueueSignalHistory::Attempt attempt(*device->queue_signals);
    for(uint32_t i=0;i<count;++i)record_application_signals(attempt,submits[i]);
    const auto result=call();
    attempt.finish(result);
    return result;
}
}
