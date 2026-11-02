#include "soundshare/desktop_client.hpp"
#include "soundshare/platform.hpp"
#include "soundshare/pipe.hpp"
#include <shlobj.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace soundshare::desktop {
Json read_json_file(const std::string& path) {
    std::ifstream file(std::filesystem::path(widen(path)),std::ios::binary);
    if(!file) throw std::runtime_error("Không mở được tệp cấu hình.");
    std::string bytes(65537,'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(file.gcount()));
    if(bytes.size()>65536) throw std::runtime_error("Tệp cấu hình quá lớn.");
    return Json::parse(bytes,[](int depth,Json::parse_event_t,Json&) {
        if(depth>32) throw std::runtime_error("Cấu hình lồng quá nhiều cấp.");return true;
    });
}
void write_json_file(const std::string& path,const Json& value) {
    const std::filesystem::path target(widen(path));
    if(!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    auto temporary=target;temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
    try {
        const auto bytes=value.dump(2);
        if(bytes.size()>65536) throw std::runtime_error("Cấu hình quá lớn để lưu.");
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        if(!file) throw std::runtime_error("Không tạo được tệp cấu hình.");
        file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();
        if(!file) throw std::runtime_error("Không ghi được cấu hình.");
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Không thay thế được tệp cấu hình.");
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
std::string default_profile_path() {
    PWSTR local=nullptr;check_hr(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local),"local application data");
    const auto path=std::filesystem::path(local)/L"SoundShare"/L"desktop.json";CoTaskMemFree(local);return narrow(path.wstring());
}
std::string friendly_error(const Json& error) {
    const auto code=error.value("code","");
    if(code=="device_unavailable") return "Thiết bị chưa sẵn sàng. Kiểm tra kết nối rồi thử lại.";
    if(code=="feedback_route") return "Nguồn âm thanh không thể đồng thời là đầu ra.";
    if(code=="receiver_timeout") return "Thiết bị nguồn chưa phản hồi. Kiểm tra Bluetooth trên điện thoại hoặc TV box.";
    if(code=="receiver_open_failed" || code=="receiver_unavailable") return "Chưa mở được nhận âm thanh. Kiểm tra nguồn trong cài đặt Bluetooth.";
    if(code=="not_running") return "Phiên chia sẻ đã dừng. Bắt đầu lại để áp dụng thay đổi.";
    if(code=="already_running") return "Dừng phiên hiện tại trước khi thay cấu hình nguồn.";
    if(code=="version_mismatch") return "Giao diện và bộ xử lý khác phiên bản. Hãy đóng bộ xử lý cũ và mở lại ứng dụng.";
    if(code=="worker_missing") return "Thiếu SoundShare.Worker.exe. Đặt bộ xử lý cạnh ứng dụng rồi mở lại.";
    if(code=="windows_error" || code=="pipe_error") return "Chưa liên lạc được với thiết bị hoặc bộ xử lý. Xem Chẩn đoán để biết chi tiết.";
    if(code=="cancelled") return "Thao tác đã được dừng.";
    return error.value("message","Không hoàn thành được thao tác. Xem Chẩn đoán để biết chi tiết.");
}
namespace {
struct RpcError : std::runtime_error {
    Json detail;
    explicit RpcError(Json value) : std::runtime_error(value.value("message","Backend error")),detail(std::move(value)) {}
};
}
struct Client::Impl {
    HWND target;
    std::mutex mutex;
    std::condition_variable condition;
    JobQueue queue;
    bool stopping=false,connected=false,ever_connected=false,owns_worker=false,restarted=false;
    std::uint64_t sequence=0;
    Handle cancel=make_event(true),process;
    std::string name=current_pipe_name(),instance_id;
    std::thread thread;
    explicit Impl(HWND window) : target(window),thread([this]{run();}) {}
    Json rpc(const std::string& method,const Json& params) {
        const auto id=++sequence;
        std::string response;
        try {response=pipe_request(name,Json{{"protocol",1},{"id",id},{"method",method},{"params",params}}.dump(),cancel.get());}
        catch(...) {connected=false;throw;}
        const auto envelope=Json::parse(response);
        if(envelope.value("protocol",0)!=1 || !envelope.contains("id") || envelope.at("id")!=id)
            throw std::runtime_error("Bộ xử lý trả lời không đúng phiên điều khiển.");
        if(!envelope.value("ok",false)) throw RpcError(envelope.at("error"));
        return envelope.at("result");
    }
    void launch_worker() {
        std::wstring module(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
        if(!size || size>=module.size()) throw BackendError("worker_missing","Không tìm thấy thư mục ứng dụng.");
        module.resize(size);
        const auto worker=std::filesystem::path(module).parent_path()/L"SoundShare.Worker.exe";
        if(!std::filesystem::exists(worker)) throw BackendError("worker_missing","Thiếu SoundShare.Worker.exe.");
        std::wstring command=L"\""+worker.wstring()+L"\" --pipe "+widen(name);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,worker.parent_path().c_str(),&startup,&info))
            check_hr(HRESULT_FROM_WIN32(GetLastError()),"start worker");
        CloseHandle(info.hThread);process=Handle(info.hProcess);owns_worker=true;
    }
    void ensure_connected() {
        if(process && WaitForSingleObject(process.get(),0)==WAIT_OBJECT_0) {process=Handle();owns_worker=false;connected=false;}
        if(connected) return;
        const auto path=pipe_path(name);
        if(!WaitNamedPipeW(path.c_str(),100)) {
            const auto error=GetLastError();
            if((error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) && !process) launch_worker();
        }
        const auto hello=rpc("hello",Json::object());
        if(hello.value("version","")!=SOUNDSHARE_VERSION) throw BackendError("version_mismatch","Giao diện và bộ xử lý khác phiên bản.");
        const auto instance=hello.at("instance_id").get<std::string>();
        restarted=ever_connected && instance!=instance_id;
        instance_id=instance;connected=true;ever_connected=true;
        if(restarted) {std::lock_guard lock(mutex);queue.worker_restarted();}
    }
    void run_job(Reply& reply) {
        const auto& job=reply.job;
        if(job.kind==JobKind::save) {write_json_file(job.path,job.params);reply.data={{"saved",true}};return;}
        if(job.kind==JobKind::load) {reply.data=read_json_file(job.path);return;}
        if(job.kind==JobKind::exit) {
            if(process && WaitForSingleObject(process.get(),0)==WAIT_OBJECT_0){process=Handle();owns_worker=false;connected=false;}
            if(!connected && !process) return;
        }
        ensure_connected();reply.online=true;
        if(restarted && job.kind==JobKind::command && job.method!="session.stop")
            throw BackendError("worker_restarted","Bộ xử lý vừa khởi động lại. Kiểm tra cấu hình rồi bắt đầu phiên mới.");
        if(job.kind==JobKind::bootstrap || job.kind==JobKind::refresh) {
            reply.data["inventory"]=rpc("inventory",Json::object());
            try {reply.data["sources"]=rpc("receiver.list",Json::object());}
            catch(const RpcError& error) {reply.data["source_error"]=error.detail;}
            reply.data["status"]=rpc("status",Json::object());
            if(job.kind==JobKind::bootstrap || restarted) reply.data["configuration"]=rpc("config.current",Json::object());
        } else if(job.kind==JobKind::poll) {
            reply.data=rpc("status",Json::object());
            if(restarted) reply.data["configuration"]=rpc("config.current",Json::object());
        }
        else if(job.kind==JobKind::exit) {
            rpc("session.stop",Json::object());
            if(owns_worker) {
                rpc("shutdown",Json::object());
                if(WaitForSingleObject(process.get(),5000)==WAIT_TIMEOUT) {
                    // Only a worker created by this instance may be terminated after explicit exit.
                    TerminateProcess(process.get(),1);WaitForSingleObject(process.get(),3000);
                }
                process=Handle();owns_worker=false;connected=false;
            }
        } else reply.data=rpc(job.method,job.params);
    }
    void run() noexcept {
        while(true) {
            Job job;
            {std::unique_lock lock(mutex);condition.wait(lock,[&]{return stopping || !queue.empty();});
                if(stopping) break;job=queue.pop();}
            auto reply=std::make_unique<Reply>();reply->job=std::move(job);
            try {run_job(*reply);}
            catch(const RpcError& error) {reply->error=error.detail;}
            catch(const BackendError& error) {reply->error={{"code",error.code},{"message",error.what()},{"hresult",hr_string(error.hresult)}};}
            catch(const std::exception& error) {reply->error={{"code","desktop_error"},{"message",error.what()}};}
            catch(...) {reply->error={{"code","desktop_error"},{"message","Không hoàn thành được thao tác."}};}
            reply->online=connected;reply->restarted=std::exchange(restarted,false);
            if(reply->job.kind==JobKind::exit && !reply->error.empty()) {std::lock_guard lock(mutex);queue.exit_failed();}
            {std::lock_guard lock(mutex);if(stopping) break;}
            if(PostMessageW(target,reply_message,0,reinterpret_cast<LPARAM>(reply.get()))) reply.release();
        }
    }
};
Client::Client(HWND target) : impl_(std::make_unique<Impl>(target)) {}
Client::~Client() {stop();}
void Client::submit(Job job) {
    std::lock_guard lock(impl_->mutex);
    if(impl_->stopping) return;
    impl_->queue.push(std::move(job));impl_->condition.notify_one();
}
void Client::stop() {
    if(!impl_ || !impl_->thread.joinable()) return;
    {std::lock_guard lock(impl_->mutex);impl_->stopping=true;impl_->queue.clear();}
    SetEvent(impl_->cancel.get());impl_->condition.notify_all();impl_->thread.join();
    MSG message{};while(PeekMessageW(&message,impl_->target,reply_message,reply_message,PM_REMOVE)) delete reinterpret_cast<Reply*>(message.lParam);
}
} // namespace soundshare::desktop
