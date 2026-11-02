#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <windows.h>

namespace soundshare {
struct ReceiverDevice { std::string id,name; };
struct ReceiverStatus {
    std::string id,state,error;
    bool desired{},auto_reconnect{};
    unsigned retries{};
};
// Small transport boundary permits deterministic lifecycle/cancellation tests without a radio.
class ReceiverConnection {
public:
    virtual ~ReceiverConnection()=default;
    virtual bool opened() const=0;
    virtual void open(HANDLE stop,HANDLE cancel)=0;
    virtual void close() noexcept=0;
};
using ReceiverFactory=std::function<std::shared_ptr<ReceiverConnection>(const std::string&)>;
class ReceiverManager {
public:
    ReceiverManager();
    explicit ReceiverManager(ReceiverFactory factory);
    ~ReceiverManager();
    std::vector<ReceiverDevice> devices();
    void open(const std::string& id,bool auto_reconnect);
    void close(const std::string& id);
    void close_all();
    std::vector<ReceiverStatus> status();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace soundshare
