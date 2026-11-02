#include "soundshare/platform.hpp"
#include "soundshare/audio.hpp"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <sddl.h>
#include <winrt/base.h>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace soundshare {
std::string hr_string(HRESULT value) {
    std::ostringstream text; text<<"0x"<<std::hex<<std::uppercase<<static_cast<unsigned long>(value); return text.str();
}
void check_hr(HRESULT value,const char* context) {
    if(FAILED(value)) throw BackendError("windows_error",std::string(context)+": "+hr_string(value),value);
}
std::wstring widen(const std::string& value) {
    if(value.empty()) return {};
    if(value.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) throw BackendError("invalid_argument","string too large");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!count) throw BackendError("invalid_argument","invalid UTF-8 string");
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
std::string narrow(const std::wstring& value) {
    if(value.empty()) return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!count) throw BackendError("invalid_argument","invalid UTF-16 string");
    std::string result(static_cast<std::size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
    return result;
}
std::uint64_t qpc_100ns() noexcept {
    static const auto frequency=[] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const auto seconds=now.QuadPart/frequency;
    const auto remainder=now.QuadPart%frequency;
    return static_cast<std::uint64_t>(seconds)*10000000ULL+static_cast<std::uint64_t>(remainder*10000000LL/frequency);
}
ComApartment::ComApartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
ComApartment::~ComApartment() { winrt::uninit_apartment(); }
Handle::~Handle() { if(*this) CloseHandle(handle_); }
Handle::Handle(Handle&& other) noexcept : handle_(std::exchange(other.handle_,nullptr)) {}
Handle& Handle::operator=(Handle&& other) noexcept {
    if(this!=&other) { if(*this) CloseHandle(handle_); handle_=std::exchange(other.handle_,nullptr); } return *this;
}
Handle make_event(bool manual) {
    Handle event(CreateEventW(nullptr,manual,FALSE,nullptr));
    if(!event) check_hr(HRESULT_FROM_WIN32(GetLastError()),"CreateEvent");
    return event;
}
static ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> result;
    check_hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&result)),"create endpoint enumerator");
    return result;
}
static std::wstring device_id(IMMDevice* device) {
    LPWSTR id=nullptr; check_hr(device->GetId(&id),"endpoint ID");
    std::wstring result(id); CoTaskMemFree(id); return result;
}
std::vector<Endpoint> list_endpoints() {
    auto owner=enumerator();
    ComPtr<IMMDeviceCollection> devices;
    check_hr(owner->EnumAudioEndpoints(eAll,DEVICE_STATEMASK_ALL,&devices),"enumerate endpoints");
    std::wstring defaults[2][3];
    for(int flow=0;flow<2;++flow) for(int role=0;role<3;++role) {
        ComPtr<IMMDevice> device;
        if(SUCCEEDED(owner->GetDefaultAudioEndpoint(static_cast<EDataFlow>(flow),static_cast<ERole>(role),&device)))
            defaults[flow][role]=device_id(device.Get());
    }
    UINT count=0; check_hr(devices->GetCount(&count),"endpoint count");
    std::vector<Endpoint> result;
    for(UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> device; check_hr(devices->Item(i,&device),"endpoint item");
        Endpoint endpoint;
        const auto id=device_id(device.Get()); endpoint.id=narrow(id);
        check_hr(device->GetState(&endpoint.state),"endpoint state");
        ComPtr<IMMEndpoint> info; check_hr(device.As(&info),"endpoint direction interface");
        EDataFlow flow=eAll; check_hr(info->GetDataFlow(&flow),"endpoint direction");
        endpoint.direction=flow==eRender ? "render" : "capture";
        endpoint.default_console=id==defaults[flow][eConsole];
        endpoint.default_multimedia=id==defaults[flow][eMultimedia];
        endpoint.default_communications=id==defaults[flow][eCommunications];
        ComPtr<IPropertyStore> properties;
        if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))) {
            PROPVARIANT value; PropVariantInit(&value);
            if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value)) && value.vt==VT_LPWSTR)
                endpoint.name=narrow(value.pwszVal);
            PropVariantClear(&value);
        }
        if(endpoint.state==DEVICE_STATE_ACTIVE) {
            ComPtr<IAudioClient> client;
            if(SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client))) {
                WAVEFORMATEX* format=nullptr;
                if(SUCCEEDED(client->GetMixFormat(&format))) {
                    endpoint.native_rate=format->nSamplesPerSec; endpoint.native_channels=format->nChannels; CoTaskMemFree(format);
                }
            }
        }
        result.push_back(std::move(endpoint));
    }
    return result;
}
void validate_endpoint(const std::string& id,EDataFlow expected) {
    if(id.empty() || id.size()>4096) throw BackendError("invalid_argument","endpoint ID is empty or too long");
    auto owner=enumerator(); ComPtr<IMMDevice> device;
    check_hr(owner->GetDevice(widen(id).c_str(),&device),"find endpoint");
    ComPtr<IMMEndpoint> info; check_hr(device.As(&info),"endpoint direction interface");
    EDataFlow direction=eAll; check_hr(info->GetDataFlow(&direction),"endpoint direction");
    if(direction!=expected) throw BackendError("invalid_route","endpoint has the wrong direction");
    DWORD state=0; check_hr(device->GetState(&state),"endpoint state");
    if(state!=DEVICE_STATE_ACTIVE) throw BackendError("device_unavailable","endpoint is not active");
}
OpenedAudio open_audio(const std::string& id,bool capture,bool loopback) {
    auto owner=enumerator(); ComPtr<IMMDevice> device;
    check_hr(owner->GetDevice(widen(id).c_str(),&device),"find audio endpoint");
    OpenedAudio result; result.event=make_event();
    check_hr(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&result.client),"activate WASAPI");
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT; format.nChannels=channels; format.nSamplesPerSec=sample_rate;
    format.wBitsPerSample=32; format.nBlockAlign=channels*sizeof(float); format.nAvgBytesPerSec=sample_rate*format.nBlockAlign;
    DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if(capture && loopback) flags|=AUDCLNT_STREAMFLAGS_LOOPBACK;
    check_hr(result.client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,0,0,&format,nullptr),"initialize shared 48 kHz stereo float WASAPI");
    check_hr(result.client->SetEventHandle(result.event.get()),"set audio event");
    check_hr(result.client->GetBufferSize(&result.frames),"audio buffer size");
    check_hr(result.client->GetStreamLatency(&result.latency),"stream latency");
    return result;
}
std::wstring current_user_sid() {
    HANDLE raw=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw)) check_hr(HRESULT_FROM_WIN32(GetLastError()),"process token");
    Handle token(raw); DWORD bytes=0;
    GetTokenInformation(token.get(),TokenUser,nullptr,0,&bytes);
    std::vector<unsigned char> buffer(bytes);
    if(!GetTokenInformation(token.get(),TokenUser,buffer.data(),bytes,&bytes)) check_hr(HRESULT_FROM_WIN32(GetLastError()),"user SID");
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)) check_hr(HRESULT_FROM_WIN32(GetLastError()),"SID text");
    std::wstring result(sid); LocalFree(sid); return result;
}
std::string current_pipe_name() {
    DWORD session=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)) check_hr(HRESULT_FROM_WIN32(GetLastError()),"session ID");
    return "SoundShare."+narrow(current_user_sid())+"."+std::to_string(session);
}
} // namespace soundshare
