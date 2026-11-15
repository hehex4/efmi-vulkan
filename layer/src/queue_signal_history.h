#pragma once
#include "common.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace efvk {
// Host-owned classification history, not a semaphore payload or hazard graph.
// Live semaphore objects bound the map; lifetime hooks erase their entries. A
// failed submission must not evict another object's successful signal history.
class QueueSignalHistory {
    struct Entry {size_t pending=0;bool committed=false;};
    std::mutex mutex_;
    std::unordered_map<VkSemaphore,std::shared_ptr<Entry>> entries_;
public:
    class Read {
        QueueSignalHistory& history_;
        std::unique_lock<std::mutex> lock_;
    public:
        explicit Read(QueueSignalHistory& history):history_(history),lock_(history.mutex_){}
        Read(QueueSignalHistory& history,std::try_to_lock_t):history_(history),lock_(history.mutex_,std::try_to_lock){}
        bool owns_lock()const{return lock_.owns_lock();}
        bool contains(VkSemaphore semaphore)const{return lock_.owns_lock()&&history_.entries_.count(semaphore)!=0;}
        void unlock(){lock_.unlock();}
    };
    Read read(){return Read(*this);}
    Read try_read(){return Read(*this,std::try_to_lock);}
    void forget(VkSemaphore semaphore){std::lock_guard<std::mutex> lock(mutex_);entries_.erase(semaphore);}
    class Attempt {
        QueueSignalHistory& history_;
        std::vector<std::pair<VkSemaphore,std::shared_ptr<Entry>>> signals_;
        bool finished_=false;
    public:
        explicit Attempt(QueueSignalHistory& history):history_(history){}
        ~Attempt(){finish(VK_ERROR_UNKNOWN);}
        Attempt(const Attempt&)=delete;
        Attempt& operator=(const Attempt&)=delete;
        void signal(VkSemaphore semaphore){
            if(finished_ || !semaphore)return;
            std::lock_guard<std::mutex> lock(history_.mutex_);
            // Reserve before publishing a contribution: allocation failure cannot
            // leave an entry that this attempt's destructor does not know about.
            if(signals_.size()==signals_.capacity())signals_.reserve(signals_.empty()?16:signals_.size()*2);
            auto found=history_.entries_.find(semaphore);
            if(found==history_.entries_.end()){
                auto entry=std::make_shared<Entry>();
                found=history_.entries_.emplace(semaphore,std::move(entry)).first;
            }
            signals_.emplace_back(semaphore,found->second);
            ++found->second->pending;
        }
        void finish(VkResult result){
            if(finished_)return;
            finished_=true;
            std::lock_guard<std::mutex> lock(history_.mutex_);
            for(const auto& signal:signals_){
                auto& entry=*signal.second;
                --entry.pending;
                if(result==VK_SUCCESS)entry.committed=true;
                if(entry.pending || entry.committed)continue;
                const auto current=history_.entries_.find(signal.first);
                // Destruction or handle reuse can detach the old entry.
                // Never reinsert it or erase a new object's contribution by handle.
                if(current!=history_.entries_.end() && current->second==signal.second)history_.entries_.erase(current);
            }
        }
    };
};
}
