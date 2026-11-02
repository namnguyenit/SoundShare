#include "soundshare/control.hpp"
#include "soundshare/platform.hpp"
#include "soundshare/pipe.hpp"
#include <iostream>
#include <atomic>
#include <winrt/base.h>

namespace {
std::atomic<HANDLE> stop_event{nullptr};
std::atomic<HANDLE> main_thread{nullptr};
BOOL WINAPI console_stop(DWORD event) {
    if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT || event==CTRL_CLOSE_EVENT || event==CTRL_SHUTDOWN_EVENT) {
        if(const auto event_handle=stop_event.load()) SetEvent(event_handle);
        if(const auto thread_handle=main_thread.load()) CancelSynchronousIo(thread_handle);
        return TRUE;
    }
    return FALSE;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        soundshare::ComApartment apartment; soundshare::Control control;
        auto stop=soundshare::make_event(true);
        soundshare::Handle thread_handle(OpenThread(THREAD_TERMINATE,FALSE,GetCurrentThreadId()));
        stop_event.store(stop.get()); main_thread.store(thread_handle.get()); SetConsoleCtrlHandler(console_stop,TRUE);
        struct ClearHandler {
            ~ClearHandler() { SetConsoleCtrlHandler(console_stop,FALSE); stop_event.store(nullptr); main_thread.store(nullptr); }
        } clear_handler;
        const std::wstring mode=argc>1 ? argv[1] : L"--help";
        if(mode==L"--inventory") {
            std::cout<<control.request(R"({"protocol":1,"id":1,"method":"inventory"})").dump(2)<<"\n"; return 0;
        }
        if(mode==L"--pipe") {
            const auto name=argc>2 ? soundshare::narrow(argv[2]) : soundshare::current_pipe_name();
            std::cerr<<"SoundShare worker pipe: "<<name<<"\n";
            soundshare::serve_pipe(control,name,stop.get()); return 0;
        }
        if(mode==L"--stdio") {
            std::string line; line.reserve(soundshare::max_message_bytes);
            char byte=0; bool oversized=false;
            while(!control.shutdown_requested() && std::cin.get(byte)) {
                if(byte=='\n') {
                    if(oversized) line.assign(soundshare::max_message_bytes+1,'x');
                    std::cout<<control.request(line).dump()<<std::endl; line.clear(); oversized=false;
                } else if(line.size()<soundshare::max_message_bytes) line+=byte;
                else oversized=true;
            }
            return 0;
        }
        std::cout<<"SoundShare.Worker --inventory | --stdio | --pipe [name]\n"; return mode==L"--help" ? 0 : 2;
    } catch(const winrt::hresult_error& error) { std::cerr<<winrt::to_string(error.message())<<"\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; }
    return 1;
}
