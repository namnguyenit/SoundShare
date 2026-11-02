#include "soundshare/receiver.hpp"
#include "soundshare/platform.hpp"
#include "soundshare/pipe.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundshare;
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate> void until(Predicate ready) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!ready()){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("receiver test timed out");std::this_thread::sleep_for(10ms);}
}
struct FakeConnection final : ReceiverConnection {
    std::atomic<bool> connected{false},block{false};
    std::atomic<unsigned> starts{0},finishes{0},closes{0},failures{0};
    bool opened() const override {return connected.load();}
    void open(HANDLE stop,HANDLE cancel) override {
        ++starts;
        if(block) {
            HANDLE events[]={stop,cancel};WaitForMultipleObjects(2,events,FALSE,INFINITE);
            ++finishes;throw BackendError("cancelled","fake open cancelled");
        }
        ++finishes;
        if(failures.load()){--failures;throw std::runtime_error("simulated source unavailable");}
        connected=true;
    }
    void close() noexcept override {++closes;connected=false;}
};
void cancel_inflight() {
    auto connection=std::make_shared<FakeConnection>();connection->block=true;
    ReceiverManager manager([&](const auto&){return connection;});
    const auto begin=std::chrono::steady_clock::now();manager.open("slow",false);
    require(std::chrono::steady_clock::now()-begin<200ms,"receiver.open blocked control transport");
    until([&]{return connection->starts.load()==1;});
    const auto queried=std::chrono::steady_clock::now();
    require(manager.status().at(0).state=="opening","inflight state was not exposed");
    require(std::chrono::steady_clock::now()-queried<200ms,"status blocked behind reconnect");
    manager.open("slow",false);require(connection->starts==1,"duplicate open overlapped connection");
    manager.close("slow");until([&]{return connection->finishes.load()==1;});
    require(manager.status().empty() && connection->closes==1,"closed entry resurrected");
}
void retry_and_recover() {
    auto connection=std::make_shared<FakeConnection>();connection->failures=1;
    ReceiverManager manager([&](const auto&){return connection;});manager.open("retry",true);
    until([&]{const auto status=manager.status();return !status.empty() && status[0].state=="closed" && !status[0].error.empty();});
    until([&]{return manager.status().at(0).state=="opened";});
    require(manager.status()[0].retries==1 && connection->starts==2,"reconnect attempt tracking");
    connection->connected=false;
    until([&]{return connection->starts.load()==3 && manager.status().at(0).state=="opened";});
    manager.close_all();require(manager.status().empty(),"close all left desired receivers");
}
void destructor_cancels() {
    auto connection=std::make_shared<FakeConnection>();connection->block=true;
    auto manager=std::make_unique<ReceiverManager>([&](const auto&){return connection;});manager->open("shutdown",false);
    until([&]{return connection->starts.load()==1;});
    const auto begin=std::chrono::steady_clock::now();manager.reset();
    require(std::chrono::steady_clock::now()-begin<500ms,"destructor waited for Bluetooth timeout");
    require(connection->finishes==1 && connection->closes==1,"shutdown did not cancel/release receiver");
}
void cancel_pipe_wait() {
    auto cancel=make_event(true);
    std::thread signal([&]{std::this_thread::sleep_for(50ms);SetEvent(cancel.get());});
    bool cancelled=false;
    const auto begin=std::chrono::steady_clock::now();
    try {pipe_request("SoundShare.Absent."+std::to_string(GetCurrentProcessId()),"{}",cancel.get());}
    catch(const BackendError& error){cancelled=error.code=="cancelled";}
    catch(...){signal.join();throw;}
    signal.join();require(cancelled,"waiting pipe request ignored cancellation");
    require(std::chrono::steady_clock::now()-begin<500ms,"cancelled request waited for connection timeout");
}
int main() {
    try {
        ComApartment apartment;
        cancel_inflight();std::cout<<"PASS nonblocking open/status and cancellation\n";
        retry_and_recover();std::cout<<"PASS retry, failure diagnostics and recovery\n";
        destructor_cancels();std::cout<<"PASS shutdown cancellation\n";
        cancel_pipe_wait();std::cout<<"PASS control transport cancellation\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
