#include "soundshare/control.hpp"
#include "soundshare/engine.hpp"
#include "soundshare/receiver.hpp"
#include "soundshare/platform.hpp"
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace soundshare {
using Json=nlohmann::json;
namespace {
Json parse_json(const std::string& content) {
    return Json::parse(content,[](int depth,Json::parse_event_t,Json&) {
        if(depth>32) throw BackendError("invalid_json","JSON nesting exceeds 32 levels");
        return true;
    });
}
void fields(const Json& value,std::initializer_list<const char*> allowed) {
    if(!value.is_object()) throw BackendError("invalid_argument","expected a JSON object");
    for(auto it=value.begin();it!=value.end();++it) {
        if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key) { return it.key()==key; }))
            throw BackendError("invalid_argument","unknown field: "+it.key());
    }
}
unsigned integer(const Json& object,const char* key,unsigned fallback,unsigned low,unsigned high) {
    if(!object.contains(key)) return fallback;
    const auto& value=object.at(key);
    if(!value.is_number_integer() || value.get<double>()<low || value.get<double>()>high)
        throw BackendError("invalid_argument",std::string(key)+" is out of range");
    return value.get<unsigned>();
}
std::string text(const Json& object,const char* key) {
    if(!object.contains(key) || !object.at(key).is_string()) throw BackendError("invalid_argument",std::string(key)+" must be a string");
    auto value=object.at(key).get<std::string>();
    if(value.empty() || value.size()>4096 || value.find('\0')!=std::string::npos)
        throw BackendError("invalid_argument",std::string(key)+" is empty or too long");
    widen(value); return value;
}
OutputConfig read_output(const Json& value) {
    fields(value,{"id","gain","muted","delay_ms"}); OutputConfig result;
    result.id=text(value,"id"); result.delay_ms=integer(value,"delay_ms",0,0,1000);
    if(value.contains("gain")) {
        if(!value.at("gain").is_number()) throw BackendError("invalid_argument","gain must be numeric");
        const auto gain=value.at("gain").get<double>();
        if(!std::isfinite(gain) || gain<0 || gain>1) throw BackendError("invalid_argument","gain must be 0..1");
        result.gain=static_cast<float>(gain);
    }
    if(value.contains("muted")) {
        if(!value.at("muted").is_boolean()) throw BackendError("invalid_argument","muted must be boolean");
        result.muted=value.at("muted").get<bool>();
    }
    return result;
}
EngineConfig read_config(const Json& value) {
    fields(value,{"version","capture","buffer_ms","master_gain","outputs"});
    if(integer(value,"version",1,1,1)!=1) throw BackendError("invalid_config","unsupported config version");
    EngineConfig result; result.buffer_ms=integer(value,"buffer_ms",120,20,500);
    if(value.contains("master_gain")) {
        if(!value.at("master_gain").is_number()) throw BackendError("invalid_argument","master_gain must be numeric");
        const double gain=value.at("master_gain").get<double>();
        if(!std::isfinite(gain) || gain<0 || gain>1) throw BackendError("invalid_config","master_gain must be 0..1");
        result.master_gain=static_cast<float>(gain);
    }
    if(!value.contains("capture")) throw BackendError("invalid_config","capture is required");
    const auto& capture=value.at("capture"); fields(capture,{"id","mode","bridge_render_id"});
    result.capture_id=text(capture,"id"); const auto mode=text(capture,"mode");
    if(mode!="loopback" && mode!="capture") throw BackendError("invalid_config","capture mode must be capture or loopback");
    result.loopback=mode=="loopback";
    if(capture.contains("bridge_render_id")) result.bridge_render_id=text(capture,"bridge_render_id");
    if(!value.contains("outputs") || !value.at("outputs").is_array()) throw BackendError("invalid_config","outputs must be an array");
    for(const auto& output:value.at("outputs")) result.outputs.push_back(read_output(output));
    validate_config(result); return result;
}
Json write_output(const OutputConfig& value) {
    return {{"id",value.id},{"gain",value.gain},{"muted",value.muted},{"delay_ms",value.delay_ms}};
}
Json write_config(const EngineConfig& config) {
    Json outputs=Json::array(); for(const auto& output:config.outputs) outputs.push_back(write_output(output));
    Json capture={{"id",config.capture_id},{"mode",config.loopback ? "loopback" : "capture"}};
    if(!config.bridge_render_id.empty()) capture["bridge_render_id"]=config.bridge_render_id;
    return {{"version",1},{"capture",capture},{"buffer_ms",config.buffer_ms},{"master_gain",config.master_gain},{"outputs",outputs}};
}
Json stream_json(const StreamStatus& status) {
    return {{"id",status.id},{"state",status.state},{"error",status.error},{"hresult",hr_string(status.hresult)},
        {"retries",status.retries},{"stream_latency_ms",status.stream_latency_ms},{"frames",status.frames},
        {"queued_frames",status.queued_frames},{"dropped_frames",status.dropped_frames},{"underruns",status.underruns},
        {"recoveries",status.recoveries},{"drift_ppm",status.drift_ppm},{"clock_ratio",status.clock_ratio},
        {"clock_valid",status.clock_valid},{"peak",status.peak},{"recent_errors",status.recent_errors}};
}
Json engine_json(const EngineStatus& status) {
    Json outputs=Json::array(); for(const auto& output:status.outputs) outputs.push_back(stream_json(output));
    return {{"active",status.active},{"all_streams_running",status.all_streams_running},
        {"capture",stream_json(status.capture)},{"outputs",outputs},{"discontinuities",status.discontinuities},
        {"timestamp_errors",status.timestamp_errors},{"idle_silence_frames",status.idle_silence_frames},
        {"device_change_epoch",status.device_change_epoch},{"acoustic_output_verified",false},
        {"bluetooth_packet_loss_available",false},{"master_gain",status.master_gain}};
}
Json endpoints_json() {
    Json result=Json::array();
    for(const auto& endpoint:list_endpoints()) result.push_back({{"id",endpoint.id},{"name",endpoint.name},
        {"direction",endpoint.direction},{"state",endpoint.state},{"active",endpoint.state==DEVICE_STATE_ACTIVE},
        {"native_sample_rate",endpoint.native_rate},{"native_channels",endpoint.native_channels},
        {"default_console",endpoint.default_console},{"default_multimedia",endpoint.default_multimedia},
        {"default_communications",endpoint.default_communications}});
    return result;
}
Json receiver_status(ReceiverManager& manager) {
    Json result=Json::array();
    for(const auto& source:manager.status()) result.push_back({{"id",source.id},{"state",source.state},
        {"desired",source.desired},{"auto_reconnect",source.auto_reconnect},{"retries",source.retries},
        {"error",source.error},{"individual_pcm_available",false},{"playing_verified",false}});
    return result;
}
Json load_json(const std::string& path) {
    std::ifstream file(std::filesystem::path(widen(path)),std::ios::binary);
    if(!file) throw BackendError("file_error","cannot open configuration file");
    std::string content(max_message_bytes+1,'\0'); file.read(content.data(),static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    if(content.size()>max_message_bytes) throw BackendError("message_too_large","configuration is larger than 64 KiB");
    return parse_json(content);
}
void save_json(const std::string& path,const Json& value) {
    const std::filesystem::path target(widen(path));
    auto temporary=target; temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
    try {
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        if(!file) throw BackendError("file_error","cannot create temporary file");
        const auto bytes=value.dump(2); file.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); file.close();
        if(!file) throw BackendError("file_error","failed to write file");
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            check_hr(HRESULT_FROM_WIN32(GetLastError()),"replace saved file");
    } catch(...) { std::error_code ignored; std::filesystem::remove(temporary,ignored); throw; }
}
}
struct Control::Impl {
    const std::string instance_id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(qpc_100ns());
    AudioEngine engine;
    ReceiverManager receivers;
    bool shutdown=false;
    std::unique_ptr<EngineConfig> staged;
    Json dispatch(const std::string& method,const Json& params) {
        if(method=="hello") {
            fields(params,{});
            return {{"name","SoundShare.Worker"},{"version",SOUNDSHARE_VERSION},{"protocol",1},{"sample_rate",48000},
                {"channels",2},{"max_outputs",8},{"per_source_pcm",false},{"simultaneous_sources_verified",false},
                {"default_endpoint_mutation",false},{"pipe",current_pipe_name()},{"instance_id",instance_id}};
        }
        if(method=="inventory") { fields(params,{}); return {{"endpoints",endpoints_json()}}; }
        if(method=="receiver.list") {
            fields(params,{}); Json devices=Json::array();
            for(const auto& device:receivers.devices()) devices.push_back({{"id",device.id},{"name",device.name}});
            return {{"devices",devices},{"connections",receiver_status(receivers)}};
        }
        if(method=="receiver.open") {
            fields(params,{"id","auto_reconnect"}); bool reconnect=false;
            if(params.contains("auto_reconnect")) {
                if(!params.at("auto_reconnect").is_boolean()) throw BackendError("invalid_argument","auto_reconnect must be boolean");
                reconnect=params.at("auto_reconnect").get<bool>();
            }
            receivers.open(text(params,"id"),reconnect); return {{"accepted",true},{"connections",receiver_status(receivers)}};
        }
        if(method=="receiver.close") { fields(params,{"id"}); receivers.close(text(params,"id")); return {{"connections",receiver_status(receivers)}}; }
        if(method=="status") { fields(params,{}); return {{"engine",engine_json(engine.status())},{"receivers",receiver_status(receivers)}}; }
        if(method=="session.start") {
            fields(params,{"config"});
            if(params.contains("config")) engine.start(read_config(params.at("config")));
            else if(staged) engine.start(*staged);
            else throw BackendError("invalid_config","provide config or load one first");
            return {{"accepted",true},{"engine",engine_json(engine.status())}};
        }
        if(method=="session.stop") { fields(params,{}); receivers.close_all(); engine.stop(); return {{"stopped",true}}; }
        if(method=="output.add") { auto output=read_output(params); engine.add_output(output); return {{"accepted",true}}; }
        if(method=="output.remove") { fields(params,{"id"}); engine.remove_output(text(params,"id")); return {{"removed",true}}; }
        if(method=="output.configure") { engine.configure_output(read_output(params)); return {{"accepted",true}}; }
        if(method=="master.configure") {
            fields(params,{"gain"});
            if(!params.contains("gain") || !params.at("gain").is_number()) throw BackendError("invalid_argument","gain must be numeric");
            const auto gain=params.at("gain").get<double>();
            if(!std::isfinite(gain) || gain<0 || gain>1) throw BackendError("invalid_argument","gain must be 0..1");
            engine.set_master_gain(static_cast<float>(gain)); return {{"accepted",true}};
        }
        if(method=="config.validate") { auto config=read_config(params); return {{"valid",true},{"config",write_config(config)}}; }
        if(method=="config.current") {
            fields(params,{});
            const bool active=engine.status().active;
            return {{"active",active},{"config",write_config(!active && staged ? *staged : engine.configuration())}};
        }
        if(method=="config.load") {
            fields(params,{"path"});
            if(engine.status().active) throw BackendError("already_running","stop the session before loading configuration");
            auto config=read_config(load_json(text(params,"path")));
            staged=std::make_unique<EngineConfig>(std::move(config)); return {{"config",write_config(*staged)}};
        }
        if(method=="config.save") {
            fields(params,{"path","config"});
            auto config=params.contains("config") ? read_config(params.at("config")) :
                (!engine.status().active && staged ? *staged : engine.configuration());
            validate_config(config); save_json(text(params,"path"),write_config(config)); return {{"saved",true}};
        }
        if(method=="diagnostics.export") {
            fields(params,{"path"});
            Json data={{"version",1},{"time_qpc_100ns",qpc_100ns()},{"engine",engine_json(engine.status())},
                {"config",write_config(engine.configuration())},{"receivers",receiver_status(receivers)},
                {"endpoints",endpoints_json()},{"contains_audio_recording",false}};
            save_json(text(params,"path"),data); return {{"saved",true}};
        }
        if(method=="audio.probe") {
            fields(params,{"id","mode"}); const auto id=text(params,"id"),mode=text(params,"mode");
            if(mode!="render" && mode!="capture" && mode!="loopback") throw BackendError("invalid_argument","mode must be render, capture or loopback");
            validate_endpoint(id,mode=="capture" ? eCapture : eRender);
            auto audio=open_audio(id,mode!="render",mode=="loopback");
            return {{"initialized",true},{"started",false},{"buffer_frames",audio.frames},{"stream_latency_ms",audio.latency/10000.0}};
        }
        if(method=="shutdown") { fields(params,{}); receivers.close_all(); engine.stop(); shutdown=true; return {{"shutdown",true}}; }
        throw BackendError("unknown_method","unknown method: "+method);
    }
};
Control::Control() : impl_(std::make_unique<Impl>()) {}
Control::~Control()=default;
bool Control::shutdown_requested() const noexcept { return impl_->shutdown; }
Json Control::request(const std::string& line) noexcept {
    Json id=nullptr;
    const auto failure=[&](const std::string& code,const std::string& message,HRESULT hr=S_OK) {
        return Json{{"protocol",1},{"id",id},{"ok",false},{"error",{{"code",code},{"message",message},{"hresult",hr_string(hr)}}}};
    };
    try {
        if(line.size()>max_message_bytes) return failure("message_too_large","request is larger than 64 KiB");
        auto command=parse_json(line); fields(command,{"protocol","id","method","params"});
        if(command.contains("id")) {
            const auto& value=command.at("id");
            if(!value.is_null() && !value.is_string() && !value.is_number_integer()) return failure("invalid_request","id must be a string or integer");
            id=value;
        }
        if(!command.contains("protocol") || !command.at("protocol").is_number_integer() || command.at("protocol")!=1)
            return failure("protocol_version","protocol must be 1");
        const auto method=text(command,"method");
        const auto params=command.value("params",Json::object());
        if(!params.is_object()) return failure("invalid_argument","params must be an object");
        return {{"protocol",1},{"id",id},{"ok",true},{"result",impl_->dispatch(method,params)}};
    } catch(const BackendError& error) { return failure(error.code,error.what(),error.hresult); }
    catch(const winrt::hresult_error& error) { return failure("windows_error",winrt::to_string(error.message()),error.code()); }
    catch(const Json::parse_error& error) { return failure("invalid_json",error.what()); }
    catch(const Json::exception& error) { return failure("invalid_argument",error.what()); }
    catch(const std::exception& error) { return failure("internal_error",error.what()); }
    catch(...) { return failure("internal_error","unexpected backend error"); }
}
} // namespace soundshare
