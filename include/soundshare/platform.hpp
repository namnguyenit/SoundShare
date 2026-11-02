#pragma once
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace soundshare {
using Microsoft::WRL::ComPtr;
struct BackendError : std::runtime_error {
    std::string code;
    HRESULT hresult;
    BackendError(std::string code_,std::string message,HRESULT hr=S_OK)
        : std::runtime_error(std::move(message)),code(std::move(code_)),hresult(hr) {}
};
void check_hr(HRESULT value,const char* context);
std::wstring widen(const std::string& value);
std::string narrow(const std::wstring& value);
std::string hr_string(HRESULT value);
std::uint64_t qpc_100ns() noexcept;
class ComApartment {
public:
    ComApartment();
    ~ComApartment();
    ComApartment(const ComApartment&)=delete;
    ComApartment& operator=(const ComApartment&)=delete;
};
class Handle {
public:
    Handle()=default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle();
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ && handle_!=INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_{};
};
Handle make_event(bool manual=false);
struct Endpoint {
    std::string id,name,direction;
    DWORD state{};
    unsigned native_rate{},native_channels{};
    bool default_console{},default_multimedia{},default_communications{};
};
std::vector<Endpoint> list_endpoints();
void validate_endpoint(const std::string& id,EDataFlow direction);
struct OpenedAudio {
    OpenedAudio()=default;
    OpenedAudio(OpenedAudio&&)=default;
    OpenedAudio& operator=(OpenedAudio&&)=default;
    ~OpenedAudio() { if(client) client->Stop(); }
    ComPtr<IAudioClient> client;
    Handle event;
    UINT32 frames{};
    REFERENCE_TIME latency{};
};
OpenedAudio open_audio(const std::string& id,bool capture,bool loopback);
std::string current_pipe_name();
std::wstring current_user_sid();
} // namespace soundshare
