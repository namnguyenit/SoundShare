#pragma once
#include "soundshare/desktop_model.hpp"
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <utility>

namespace soundshare::desktop {
enum class JobKind { bootstrap,refresh,poll,command,save,load,exit };
struct Job {
    JobKind kind{JobKind::command};
    std::string method,key,path;
    Json params=Json::object();
};
// Caller provides synchronization. Never move adjustments across lifecycle commands.
class JobQueue {
public:
    void push(Job job) {
        if(exiting_) return;
        if(job.kind==JobKind::exit) {
            std::erase_if(queue_,[](const auto& queued){return queued.kind!=JobKind::save;});
            queue_.push_back(std::move(job));exiting_=true;return;
        }
        if(!job.key.empty()) {
            for(auto it=queue_.rbegin();it!=queue_.rend();++it) {
                if(it->kind==JobKind::load || it->kind==JobKind::bootstrap ||
                   (it->kind==JobKind::command && it->key.empty())) break;
                if(it->key==job.key && it->kind==job.kind) {*it=std::move(job);return;}
            }
        }
        if(queue_.size()>=64) throw std::runtime_error("Đang có quá nhiều thao tác. Vui lòng đợi một chút.");
        queue_.push_back(std::move(job));
    }
    Job pop() {auto job=std::move(queue_.front());queue_.pop_front();return job;}
    bool empty() const noexcept {return queue_.empty();}
    std::size_t size() const noexcept {return queue_.size();}
    void clear() noexcept {queue_.clear();}
    void exit_failed() noexcept {exiting_=false;}
    void worker_restarted() {
        std::erase_if(queue_,[](const auto& job){return job.kind==JobKind::command;});
    }
private:
    std::deque<Job> queue_;
    bool exiting_{};
};
} // namespace soundshare::desktop
