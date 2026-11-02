#include "soundshare/receiver.hpp"
#include "soundshare/platform.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Audio.h>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

namespace soundshare {
using namespace winrt::Windows::Media::Audio;
using winrt::Windows::Foundation::AsyncStatus;
namespace {
template<class Operation> void await_ready(const Operation& op,HANDLE stop,HANDLE cancel=nullptr) {
    const auto deadline=GetTickCount64()+10000;
    while(op.Status()==AsyncStatus::Started) {
        HANDLE events[]={stop,cancel};
        if(WaitForMultipleObjects(cancel ? 2U : 1U,events,FALSE,50)!=WAIT_TIMEOUT) {
            try {op.Cancel();}catch(...){}
            throw BackendError("cancelled","Bluetooth operation cancelled");
        }
        if(GetTickCount64()>=deadline) {
            try {op.Cancel();}catch(...){}
            throw BackendError("receiver_timeout","Bluetooth operation timed out");
        }
    }
}
void start_connection(const AudioPlaybackConnection& connection,HANDLE stop,HANDLE cancel) {
    if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0 || WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)
        throw BackendError("cancelled","Bluetooth connection closed");
    auto start=connection.StartAsync(); await_ready(start,stop,cancel); start.get();
    if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw BackendError("cancelled","Bluetooth connection closed");
    auto open=connection.OpenAsync(); await_ready(open,stop,cancel); const auto result=open.get();
    if(result.Status()!=AudioPlaybackConnectionOpenResultStatus::Success)
        throw BackendError("receiver_open_failed","Bluetooth receiver open status "+std::to_string(static_cast<int>(result.Status())),result.ExtendedError());
}
class WindowsReceiver final : public ReceiverConnection {
    AudioPlaybackConnection connection_;
public:
    explicit WindowsReceiver(AudioPlaybackConnection connection):connection_(std::move(connection)){}
    ~WindowsReceiver() override {close();}
    bool opened() const override {return connection_.State()==AudioPlaybackConnectionState::Opened;}
    void open(HANDLE stop,HANDLE cancel) override {start_connection(connection_,stop,cancel);}
    void close() noexcept override {try {connection_.Close();}catch(...){}}
};
}
struct ReceiverManager::Impl {
    struct Entry {
        std::shared_ptr<ReceiverConnection> connection;
        bool desired{true},reconnect{false},busy{false},pending{true};
        Handle cancel=make_event(true);
        unsigned retries{};
        std::string error;
        std::chrono::steady_clock::time_point next{};
    };
    std::mutex mutex;
    std::map<std::string,std::shared_ptr<Entry>> entries;
    Handle stop=make_event(true);
    ReceiverFactory factory;
    std::thread thread;
    explicit Impl(ReceiverFactory create) : factory(std::move(create)),thread([this] { monitor(); }) {}
    ~Impl() { SetEvent(stop.get()); if(thread.joinable()) thread.join(); }
    void monitor() noexcept {
        try {
            ComApartment apartment;
            while(WaitForSingleObject(stop.get(),500)==WAIT_TIMEOUT) {
                std::vector<std::shared_ptr<Entry>> candidates;
                {std::lock_guard lock(mutex);for(const auto& [id,entry]:entries){(void)id;candidates.push_back(entry);}}
                for(const auto& entry:candidates) {
                    {std::lock_guard lock(mutex);
                    if(!entry->desired || entry->busy || (!entry->pending && (!entry->reconnect || entry->retries>=5 ||
                       std::chrono::steady_clock::now()<entry->next))) continue;
                    entry->busy=true;}
                    std::string error;
                    bool attempted=false;
                    try {
                        if(!entry->connection->opened()) {
                            attempted=true;
                            {std::lock_guard lock(mutex);if(!entry->pending)++entry->retries;entry->pending=false;}
                            entry->connection->open(stop.get(),entry->cancel.get());
                        }
                    } catch(const std::exception& problem) { error=problem.what(); }
                    catch(const winrt::hresult_error& problem) { error=winrt::to_string(problem.message()); }
                    catch(...) {error="Unexpected Bluetooth operation error";}
                    {std::lock_guard lock(mutex);
                        entry->busy=false;entry->pending=false;
                        if(entry->desired)entry->error=std::move(error);
                        if(attempted)entry->next=std::chrono::steady_clock::now()+std::chrono::seconds(1U<<entry->retries);
                    }
                }
            }
        } catch(...) { /* Status requests still expose the original connection state. */ }
    }
};
ReceiverManager::ReceiverManager() : ReceiverManager([](const std::string& id)->std::shared_ptr<ReceiverConnection> {
    auto connection=AudioPlaybackConnection::TryCreateFromId(winrt::to_hstring(id));
    if(!connection)return {};
    return std::make_shared<WindowsReceiver>(std::move(connection));
}) {}
ReceiverManager::ReceiverManager(ReceiverFactory factory) : impl_(std::make_unique<Impl>(std::move(factory))) {}
ReceiverManager::~ReceiverManager() { close_all(); }
std::vector<ReceiverDevice> ReceiverManager::devices() {
    using winrt::Windows::Devices::Enumeration::DeviceInformation;
    auto operation=DeviceInformation::FindAllAsync(AudioPlaybackConnection::GetDeviceSelector());
    await_ready(operation,impl_->stop.get());
    std::vector<ReceiverDevice> result;
    for(const auto& device:operation.get()) result.push_back({winrt::to_string(device.Id()),winrt::to_string(device.Name())});
    return result;
}
void ReceiverManager::open(const std::string& id,bool reconnect) {
    if(id.empty() || id.size()>4096 || id.find('\0')!=std::string::npos) throw BackendError("invalid_argument","invalid receiver ID");
    std::unique_lock lock(impl_->mutex);
    auto found=impl_->entries.find(id);
    const bool created=found==impl_->entries.end();
    std::shared_ptr<Impl::Entry> entry;
    if(created) {
        if(impl_->entries.size()>=8) throw BackendError("capacity_exceeded","at most eight receiver handles are allowed");
        entry=std::make_shared<Impl::Entry>();
        entry->connection=impl_->factory(id);
        if(!entry->connection) throw BackendError("receiver_unavailable","Windows cannot create a receiver for this device");
        impl_->entries.emplace(id,entry);
    } else entry=found->second;
    entry->reconnect=reconnect;
    if(!entry->busy){entry->pending=true;entry->retries=0;entry->error.clear();}
}
void ReceiverManager::close(const std::string& id) {
    std::unique_lock lock(impl_->mutex); const auto found=impl_->entries.find(id);
    if(found==impl_->entries.end()) return;
    auto entry=found->second;entry->desired=false;SetEvent(entry->cancel.get());
    impl_->entries.erase(found);
    lock.unlock();entry->connection->close();
}
void ReceiverManager::close_all() {
    decltype(impl_->entries) detached;
    {std::lock_guard lock(impl_->mutex);detached.swap(impl_->entries);
        for(auto& [id,entry]:detached){(void)id;entry->desired=false;SetEvent(entry->cancel.get());}}
    for(auto& [id,entry]:detached){(void)id;entry->connection->close();}
}
std::vector<ReceiverStatus> ReceiverManager::status() {
    std::lock_guard lock(impl_->mutex); std::vector<ReceiverStatus> result;
    for(auto& [id,entry]:impl_->entries) {
        std::string state="closed";
        try { if(entry->connection->opened()) state="opened"; }
        catch(...) { state="unavailable"; }
        if(entry->busy || entry->pending)state="opening";
        result.push_back({id,state,entry->error,entry->desired,entry->reconnect,entry->retries});
    }
    return result;
}
} // namespace soundshare
