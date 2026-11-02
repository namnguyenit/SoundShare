#include "soundshare/desktop_model.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace soundshare::desktop {
namespace {
std::string id_text(const Json& value,const char* key) {
    if(!value.contains(key) || !value.at(key).is_string()) throw std::invalid_argument("Thiếu mã thiết bị.");
    auto id=value.at(key).get<std::string>();
    if(id.empty() || id.size()>4096 || id.find('\0')!=std::string::npos) throw std::invalid_argument("Mã thiết bị không hợp lệ.");
    return id;
}
float gain_value(const Json& object,const char* key,float fallback) {
    if(!object.contains(key)) return fallback;
    if(!object.at(key).is_number()) throw std::invalid_argument("Âm lượng phải là một số.");
    auto value=object.at(key).get<double>();
    if(!std::isfinite(value) || value<0 || value>1) throw std::invalid_argument("Âm lượng phải nằm trong khoảng 0–100%.");
    return static_cast<float>(value);
}
unsigned uint_value(const Json& object,const char* key,unsigned fallback,unsigned min,unsigned max) {
    if(!object.contains(key)) return fallback;
    const auto& value=object.at(key);
    if(!value.is_number_integer() || value.get<double>()<min || value.get<double>()>max)
        throw std::invalid_argument("Thông số bộ đệm hoặc độ trễ không hợp lệ.");
    return value.get<unsigned>();
}
}
Json output_json(const Output& output) {
    return {{"id",output.id},{"gain",output.gain},{"muted",output.muted},{"delay_ms",output.delay_ms}};
}
Output& Model::output(const std::string& id) {
    auto found=std::find_if(outputs.begin(),outputs.end(),[&](const auto& value){return value.id==id;});
    if(found==outputs.end()) throw std::invalid_argument("Không tìm thấy thiết bị phát.");
    return *found;
}
const Output& Model::output(const std::string& id) const {
    auto found=std::find_if(outputs.begin(),outputs.end(),[&](const auto& value){return value.id==id;});
    if(found==outputs.end()) throw std::invalid_argument("Không tìm thấy thiết bị phát.");
    return *found;
}
bool Model::blocked(const std::string& id) const {
    return (mode=="loopback" && capture_id==id) || (!bridge_render_id.empty() && bridge_render_id==id && mode=="capture");
}
bool Model::available(const std::string& id) const {
    return std::any_of(endpoints.begin(),endpoints.end(),[&](const auto& value){return value.id==id && value.active;});
}
std::string Model::name_of(const std::string& id) const {
    for(const auto& value:endpoints) if(value.id==id) return value.name;
    for(const auto& value:outputs) if(value.id==id) return value.name;
    return id;
}
void Model::apply_inventory(const Json& result) {
    std::vector<Endpoint> next;
    for(const auto& item:result.at("endpoints")) {
        next.push_back({item.at("id").get<std::string>(),item.at("name").get<std::string>(),item.at("direction").get<std::string>(),
            item.at("active").get<bool>(),item.value("default_multimedia",false)});
    }
    endpoints=std::move(next);
    for(const auto& endpoint:endpoints) if(endpoint.direction=="render" && endpoint.active) {
        const auto found=std::find_if(outputs.begin(),outputs.end(),[&](const auto& item){return item.id==endpoint.id;});
        if(found==outputs.end()) outputs.push_back({endpoint.id,endpoint.name});
        else found->name=endpoint.name;
    }
    std::erase_if(outputs,[&](const auto& item){return !item.selected && !available(item.id);});
    std::stable_sort(outputs.begin(),outputs.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    // Recording endpoints may be microphones. Require an explicit selection rather
    // than silently choosing one when the user intended to configure a virtual cable.
    if(capture_id.empty() && mode=="loopback") {
        const auto options=capture_options();
        auto preferred=std::find_if(options.begin(),options.end(),[](const auto& value){return value.default_multimedia;});
        if(preferred!=options.end()) capture_id=preferred->id;
        else if(!options.empty()) capture_id=options.front().id;
    }
    for(auto& item:outputs) if(blocked(item.id)) item.selected=false;
}
void Model::apply_sources(const Json& result) {
    if(result.contains("devices")) {
        for(auto& source:sources) source.available=false;
        for(const auto& device:result.at("devices")) {
            const auto id=device.at("id").get<std::string>();
            auto found=std::find_if(sources.begin(),sources.end(),[&](const auto& item){return item.id==id;});
            if(found==sources.end()) sources.push_back({id,device.at("name").get<std::string>(),"available","",true});
            else {found->name=device.at("name").get<std::string>(); found->available=true;}
        }
    }
    const auto& connections=result.contains("connections") ? result.at("connections") : result;
    if(!connections.is_array()) return;
    for(auto& source:sources) {source.state=source.available ? "available" : "unavailable";source.error.clear();}
    for(const auto& connection:connections) {
        const auto id=connection.at("id").get<std::string>();
        auto found=std::find_if(sources.begin(),sources.end(),[&](const auto& item){return item.id==id;});
        if(found==sources.end()) {sources.push_back({id,id});found=sources.end()-1;}
        found->state=connection.value("state","closed");found->error=connection.value("error","");
    }
    std::erase_if(sources,[](const auto& source){return !source.available && source.state=="unavailable";});
}
void Model::apply_status(const Json& result) {
    const auto& engine=result.at("engine");
    active=engine.value("active",false); all_running=engine.value("all_streams_running",false);
    device_epoch=engine.value("device_change_epoch",std::uint64_t{0});last_status=result;
    if(result.contains("receivers")) apply_sources(result.at("receivers"));
}
void Model::set_capture(std::string new_mode,std::string id) {
    if(active) throw std::invalid_argument("Dừng phiên chia sẻ trước khi đổi nguồn.");
    if(new_mode!="capture" && new_mode!="loopback") throw std::invalid_argument("Chế độ nguồn không hợp lệ.");
    mode=std::move(new_mode);capture_id=std::move(id);
    for(auto& item:outputs) if(blocked(item.id)) item.selected=false;
}
void Model::set_selected(const std::string& id,bool selected) {
    if(selected && blocked(id)) throw std::invalid_argument("Thiết bị này đang được dùng làm nguồn âm thanh.");
    if(selected && !output(id).selected && selected_count()>=8) throw std::invalid_argument("Chọn tối đa 8 đầu ra trong phiên này.");
    output(id).selected=selected;
}
std::vector<Endpoint> Model::capture_options() const {
    std::vector<Endpoint> result;
    for(const auto& endpoint:endpoints) if(endpoint.active && endpoint.direction==(mode=="loopback" ? "render" : "capture")) result.push_back(endpoint);
    return result;
}
std::vector<Endpoint> Model::render_options() const {
    std::vector<Endpoint> result;
    for(const auto& endpoint:endpoints) if(endpoint.active && endpoint.direction=="render") result.push_back(endpoint);
    return result;
}
unsigned Model::selected_count(bool active_only) const {
    unsigned count=0;for(const auto& item:outputs) if(item.selected && (!active_only || available(item.id))) ++count;
    return count;
}
Json Model::configuration(bool active_only) const {
    if(capture_id.empty() || (active_only && !available(capture_id))) throw std::invalid_argument("Chọn một nguồn âm thanh đang sẵn sàng.");
    Json selected=Json::array();
    for(const auto& item:outputs) if(item.selected && (!active_only || available(item.id))) {
        if(blocked(item.id)) throw std::invalid_argument("Nguồn âm thanh không thể đồng thời là đầu ra.");
        selected.push_back(output_json(item));
    }
    if(selected.empty()) throw std::invalid_argument("Chọn ít nhất một thiết bị phát đang sẵn sàng.");
    if(selected.size()>8) throw std::invalid_argument("Chọn tối đa 8 thiết bị phát.");
    Json capture={{"id",capture_id},{"mode",mode}};
    if(mode=="capture" && !bridge_render_id.empty()) capture["bridge_render_id"]=bridge_render_id;
    return {{"version",1},{"capture",capture},{"buffer_ms",buffer_ms},{"master_gain",master_gain},{"outputs",selected}};
}
void Model::adopt_session(const Json& config) {
    mode=config.at("capture").at("mode").get<std::string>();capture_id=id_text(config.at("capture"),"id");
    if(mode!="loopback" && mode!="capture") throw std::invalid_argument("Chế độ nguồn không hợp lệ.");
    if(!config.at("outputs").is_array() || config.at("outputs").size()>8)
        throw std::invalid_argument("Cấu hình chứa quá nhiều đầu ra.");
    bridge_render_id=config.at("capture").value("bridge_render_id","");
    buffer_ms=uint_value(config,"buffer_ms",120,20,500); master_gain=gain_value(config,"master_gain",0.5f);
    for(auto& item:outputs) item.selected=false;
    std::set<std::string> ids;
    for(const auto& item:config.at("outputs")) {
        const auto id=id_text(item,"id");
        if(!ids.insert(id).second) throw std::invalid_argument("Cấu hình có thiết bị trùng lặp.");
        auto found=std::find_if(outputs.begin(),outputs.end(),[&](const auto& value){return value.id==id;});
        if(found==outputs.end()) {outputs.push_back({id,name_of(id)});found=outputs.end()-1;}
        found->selected=true;found->gain=gain_value(item,"gain",1);found->muted=item.value("muted",false);
        found->delay_ms=uint_value(item,"delay_ms",0,0,1000);
    }
}
Json Model::profile() const {
    Json saved=Json::array();
    for(const auto& item:outputs) {
        auto value=output_json(item);value["name"]=item.name;value["selected"]=item.selected;saved.push_back(value);
    }
    return {{"schema","soundshare.desktop"},{"version",1},{"mode",mode},{"capture_id",capture_id},
        {"bridge_render_id",bridge_render_id},{"buffer_ms",buffer_ms},{"master_gain",master_gain},
        {"auto_reconnect",auto_reconnect},{"outputs",saved}};
}
void Model::load_profile(const Json& value) {
    if(!value.is_object() || value.value("version",0)!=1) throw std::invalid_argument("Phiên bản cấu hình không được hỗ trợ.");
    Model loaded;
    if(!value.contains("schema") && value.contains("capture")) {
        loaded.adopt_session(value);
        if(!loaded.selected_count())throw std::invalid_argument("Cấu hình phiên phải chọn ít nhất một đầu ra.");
    } else {
        if(value.value("schema","")!="soundshare.desktop") throw std::invalid_argument("Tệp không phải cấu hình SoundShare.");
        loaded.mode=value.value("mode","loopback");
        if(loaded.mode!="loopback" && loaded.mode!="capture") throw std::invalid_argument("Chế độ nguồn không hợp lệ.");
        loaded.capture_id=value.value("capture_id","");loaded.bridge_render_id=value.value("bridge_render_id","");
        loaded.buffer_ms=uint_value(value,"buffer_ms",120,20,500);loaded.master_gain=gain_value(value,"master_gain",0.5f);
        loaded.auto_reconnect=value.value("auto_reconnect",false);
        const auto& saved=value.at("outputs");
        if(!saved.is_array() || saved.size()>128) throw std::invalid_argument("Danh sách thiết bị trong cấu hình không hợp lệ.");
        std::set<std::string> ids;
        for(const auto& item:saved) {
            const auto id=id_text(item,"id");if(!ids.insert(id).second) throw std::invalid_argument("Cấu hình có thiết bị trùng lặp.");
            Output output{id,item.value("name",id),item.value("selected",false),item.value("muted",false),gain_value(item,"gain",0.75f),uint_value(item,"delay_ms",0,0,1000)};
            if(output.name.size()>4096) throw std::invalid_argument("Tên thiết bị quá dài.");
            loaded.outputs.push_back(std::move(output));
        }
    }
    if(loaded.capture_id.size()>4096 || loaded.bridge_render_id.size()>4096 || loaded.capture_id.find('\0')!=std::string::npos || loaded.bridge_render_id.find('\0')!=std::string::npos)
        throw std::invalid_argument("Mã nguồn âm thanh không hợp lệ.");
    if(loaded.selected_count()>8) throw std::invalid_argument("Cấu hình chọn quá 8 thiết bị phát.");
    for(const auto& output:loaded.outputs) if(output.selected && loaded.blocked(output.id))
        throw std::invalid_argument("Cấu hình tạo đường âm thanh quay lại nguồn.");
    const auto old_endpoints=endpoints;const auto old_sources=sources;
    *this=std::move(loaded);endpoints=old_endpoints;sources=old_sources;
    for(auto& output:outputs)for(const auto& endpoint:endpoints)if(output.id==endpoint.id)output.name=endpoint.name;
    for(const auto& endpoint:endpoints)if(endpoint.active && endpoint.direction=="render" &&
        std::none_of(outputs.begin(),outputs.end(),[&](const auto& output){return output.id==endpoint.id;}))
        outputs.push_back({endpoint.id,endpoint.name});
}
} // namespace soundshare::desktop
