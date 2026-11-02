#include "soundshare/pipe.hpp"
#include "soundshare/control.hpp"
#include "soundshare/platform.hpp"
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cctype>

namespace soundshare {
std::wstring pipe_path(const std::string& name) {
    if(name.empty() || name.size()>180 || !std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return std::isalnum(c) || c=='.' || c=='_' || c=='-';
    })) throw BackendError("invalid_argument","invalid pipe name");
    return L"\\\\.\\pipe\\"+widen(name);
}
namespace {
bool finish_io(HANDLE pipe,OVERLAPPED& operation,HANDLE stop,DWORD timeout,DWORD& transferred) {
    HANDLE events[]={stop,operation.hEvent};
    const auto result=WaitForMultipleObjects(stop ? 2 : 1,stop ? events : events+1,FALSE,timeout);
    const auto complete=stop ? WAIT_OBJECT_0+1 : WAIT_OBJECT_0;
    if(result!=complete) {
        CancelIoEx(pipe,&operation); WaitForSingleObject(operation.hEvent,INFINITE);
        if(result==WAIT_FAILED) check_hr(HRESULT_FROM_WIN32(GetLastError()),"wait pipe IO");
        return false;
    }
    if(!GetOverlappedResult(pipe,&operation,&transferred,FALSE)) {
        const auto error=GetLastError();
        if(error==ERROR_BROKEN_PIPE || error==ERROR_NO_DATA || error==ERROR_OPERATION_ABORTED) return false;
        check_hr(HRESULT_FROM_WIN32(error),"pipe IO");
    }
    return true;
}
class LineReader {
public:
    LineReader(HANDLE pipe,HANDLE stop,std::size_t limit) : pipe_(pipe),stop_(stop),limit_(limit),event_(make_event(true)) {}
    bool next(std::string& line,DWORD timeout=30000) {
        line.clear();
        while(true) {
            if(begin_==end_) {
                ResetEvent(event_.get()); OVERLAPPED operation{}; operation.hEvent=event_.get(); DWORD got=0;
                if(!ReadFile(pipe_,buffer_.data(),static_cast<DWORD>(buffer_.size()),&got,&operation)) {
                    if(GetLastError()!=ERROR_IO_PENDING || !finish_io(pipe_,operation,stop_,timeout,got)) return false;
                }
                if(!got) return false;
                begin_=0; end_=got;
            }
            const auto byte=buffer_[begin_++];
            if(byte=='\n') return true;
            if(line.size()>=limit_) return false;
            line+=byte;
        }
    }
private:
    HANDLE pipe_,stop_;
    std::size_t limit_,begin_{0},end_{0};
    std::array<char,4096> buffer_{};
    Handle event_;
};
bool write_line(HANDLE pipe,HANDLE stop,const std::string& line) {
    if(line.size()>1024*1024) throw BackendError("message_too_large","response is larger than 1 MiB");
    const auto payload=line+"\n"; std::size_t offset=0;
    while(offset<payload.size()) {
        auto event=make_event(true); OVERLAPPED operation{}; operation.hEvent=event.get(); DWORD wrote=0;
        if(!WriteFile(pipe,payload.data()+offset,static_cast<DWORD>(payload.size()-offset),&wrote,&operation)) {
            if(GetLastError()!=ERROR_IO_PENDING || !finish_io(pipe,operation,stop,30000,wrote)) return false;
        }
        if(!wrote) return false; offset+=wrote;
    }
    return true;
}
}
void serve_pipe(Control& control,const std::string& name,HANDLE stop) {
    const auto path=pipe_path(name);
    const auto descriptor=L"D:P(A;;GA;;;"+current_user_sid()+L")";
    PSECURITY_DESCRIPTOR security=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(),SDDL_REVISION_1,&security,nullptr))
        check_hr(HRESULT_FROM_WIN32(GetLastError()),"pipe security descriptor");
    struct FreeSecurity { PSECURITY_DESCRIPTOR data; ~FreeSecurity() { LocalFree(data); } } free_security{security};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),security,FALSE};
    // Keep the server instance alive across clients, preventing another process from claiming the name between clients.
    Handle pipe(CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attributes));
    if(!pipe) check_hr(HRESULT_FROM_WIN32(GetLastError()),"create control pipe");
    while(!control.shutdown_requested() && WaitForSingleObject(stop,0)!=WAIT_OBJECT_0) {
        auto event=make_event(true); OVERLAPPED operation{}; operation.hEvent=event.get();
        bool connected=ConnectNamedPipe(pipe.get(),&operation)!=FALSE;
        if(!connected) {
            const auto error=GetLastError();
            if(error==ERROR_PIPE_CONNECTED) connected=true;
            else if(error==ERROR_IO_PENDING) { DWORD ignored=0; connected=finish_io(pipe.get(),operation,stop,INFINITE,ignored); }
            else check_hr(HRESULT_FROM_WIN32(error),"connect control pipe");
        }
        if(!connected) break;
        std::string line; LineReader reader(pipe.get(),stop,max_message_bytes);
        while(!control.shutdown_requested() && reader.next(line)) {
            if(!write_line(pipe.get(),stop,control.request(line).dump())) break;
            // Let the client read the final response and close before DisconnectNamedPipe
            // can discard unread bytes. A misbehaving client cannot block shutdown forever.
            if(control.shutdown_requested()) { reader.next(line,1000); break; }
        }
        DisconnectNamedPipe(pipe.get());
    }
}
std::string pipe_request(const std::string& name,const std::string& request,HANDLE cancel) {
    if(request.size()>max_message_bytes) throw BackendError("message_too_large","request is larger than 64 KiB");
    const auto path=pipe_path(name);
    const auto deadline=GetTickCount64()+10000;
    Handle pipe;
    while(true) {
        if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw BackendError("cancelled","request cancelled");
        if(WaitNamedPipeW(path.c_str(),100)) {
            pipe=Handle(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
            if(pipe)break;
            // Another local client can claim the single instance between wait and open.
        }
        const auto error=GetLastError();
        if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) throw BackendError("cancelled","request cancelled");
        if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_SEM_TIMEOUT && error!=ERROR_PIPE_BUSY)
            check_hr(HRESULT_FROM_WIN32(error),"wait for SoundShare worker");
        if(GetTickCount64()>=deadline) check_hr(HRESULT_FROM_WIN32(error),"wait for SoundShare worker");
        if(cancel) { if(WaitForSingleObject(cancel,50)==WAIT_OBJECT_0) throw BackendError("cancelled","request cancelled"); }
        else Sleep(50);
    }
    if(!write_line(pipe.get(),cancel,request)) throw BackendError("pipe_error","cannot write request");
    std::string result; LineReader reader(pipe.get(),cancel,1024*1024);
    if(reader.next(result)) return result;
    throw BackendError("pipe_error","worker disconnected before completing its response");
}
} // namespace soundshare
