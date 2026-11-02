#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace soundshare::desktop {
using Json=nlohmann::json;
struct Endpoint {
    std::string id,name,direction;
    bool active{},default_multimedia{};
};
struct Output {
    std::string id,name;
    bool selected{},muted{};
    float gain{0.75f};
    unsigned delay_ms{};
};
struct Source {
    std::string id,name,state{"available"},error;
    bool available{};
};
class Model {
public:
    std::string mode{"loopback"},capture_id,bridge_render_id;
    unsigned buffer_ms{120};
    float master_gain{0.5f};
    bool auto_reconnect{},active{},all_running{};
    std::uint64_t device_epoch{};
    std::vector<Endpoint> endpoints;
    std::vector<Output> outputs;
    std::vector<Source> sources;
    Json last_status=Json::object();

    void apply_inventory(const Json& result);
    void apply_sources(const Json& result);
    void apply_status(const Json& result);
    void adopt_session(const Json& config);
    void set_capture(std::string new_mode,std::string id);
    void set_selected(const std::string& id,bool selected);
    bool blocked(const std::string& id) const;
    bool available(const std::string& id) const;
    Output& output(const std::string& id);
    const Output& output(const std::string& id) const;
    std::vector<Endpoint> capture_options() const;
    std::vector<Endpoint> render_options() const;
    Json configuration(bool active_only=true) const;
    Json profile() const;
    void load_profile(const Json& value);
    std::string name_of(const std::string& id) const;
    unsigned selected_count(bool active_only=false) const;
};
Json output_json(const Output& output);
Json read_json_file(const std::string& path);
void write_json_file(const std::string& path,const Json& value);
} // namespace soundshare::desktop
