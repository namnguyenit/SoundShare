#include "soundshare/desktop_model.hpp"
#include "soundshare/desktop_jobs.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace soundshare::desktop;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action action,const char* message) {
    try {action();}catch(const std::exception&){return;}throw std::runtime_error(message);
}
Json inventory() {
    return {{"endpoints",Json::array({
        {{"id","source"},{"name","Âm thanh PC"},{"direction","render"},{"active",true},{"default_multimedia",true}},
        {{"id","a"},{"name","Tai nghe A"},{"direction","render"},{"active",true}},
        {{"id","b"},{"name","Tai nghe B"},{"direction","render"},{"active",true}},
        {{"id","record"},{"name","Cáp ảo"},{"direction","capture"},{"active",true}}
    })}};
}
void routes() {
    Model model;model.apply_inventory(inventory());check(model.capture_id=="source","default capture");
    rejects([&]{model.set_selected("source",true);},"feedback source selectable");
    model.set_selected("a",true);model.set_selected("b",true);
    auto config=model.configuration();check(config.at("outputs").size()==2,"two outputs");
    check(config.at("capture").at("mode")=="loopback","loopback mode");
    model.bridge_render_id="source";model.set_capture("capture","record");
    rejects([&]{model.set_selected("source",true);},"bridge playback selectable");
    check(model.configuration().at("capture").at("bridge_render_id")=="source","bridge guard missing");
}
void disappearing_device() {
    Model model;model.apply_inventory(inventory());model.set_selected("a",true);model.set_selected("b",true);
    auto next=inventory();next["endpoints"][2]["active"]=false;model.apply_inventory(next);
    check(model.output("b").selected,"missing selection lost");
    check(model.selected_count()==2 && model.selected_count(true)==1,"availability count");
    check(model.configuration().at("outputs").size()==1,"unavailable output included in live start");
    check(model.configuration(false).at("outputs").size()==2,"saved desired output lost");
    model.apply_inventory(inventory());check(model.configuration().at("outputs").size()==2,"returning device not restored");
}
void profile_roundtrip() {
    Model original;original.apply_inventory(inventory());original.set_selected("a",true);
    original.output("a").delay_ms=123;original.output("a").muted=true;original.master_gain=0.32f;
    Model loaded;loaded.load_profile(Json::parse(original.profile().dump()));
    check(loaded.output("a").selected && loaded.output("a").muted,"selection/mute not retained");
    check(loaded.output("a").delay_ms==123,"delay not retained");check(std::abs(loaded.master_gain-0.32f)<0.001,"master not retained");
    check(!loaded.active,"loading configuration auto-started session");
    auto invalid=original.profile();invalid["master_gain"]=1.1;
    const auto before=loaded.profile();rejects([&]{loaded.load_profile(invalid);},"bad profile gain accepted");
    check(before==loaded.profile(),"failed import partially mutated state");
    invalid=original.profile();invalid["outputs"][0]["delay_ms"]=-1;rejects([&]{loaded.load_profile(invalid);},"negative delay accepted");
    invalid=original.profile();invalid["outputs"].push_back(invalid["outputs"][0]);rejects([&]{loaded.load_profile(invalid);},"duplicate profile ID accepted");
}
void import_backend_configuration() {
    Model original;original.apply_inventory(inventory());original.set_selected("a",true);
    Model loaded;loaded.apply_inventory(inventory());loaded.load_profile(original.configuration());
    check(loaded.output("a").name=="Tai nghe A","backend import lost friendly name");
    auto invalid=original.configuration();invalid["capture"]["mode"]="unknown";
    rejects([&]{loaded.load_profile(invalid);},"bad capture mode accepted");
    invalid=original.configuration();invalid["outputs"][0]["id"]="source";
    rejects([&]{loaded.load_profile(invalid);},"feedback import accepted");
}
void source_states_are_not_playback_proof() {
    Model model;
    model.apply_sources({{"devices",Json::array({{{"id","phone"},{"name","Điện thoại"}}})},{"connections",Json::array()}});
    check(model.sources.size()==1 && model.sources[0].state=="available","discovery state");
    model.apply_sources(Json::array({{{"id","phone"},{"state","opened"}}}));
    check(model.sources[0].state=="opened","connection state");
    check(!model.active,"opening receiver implied session playback");
    model.apply_sources(Json::array());check(model.sources[0].state=="available","closed connection not cleared");
}
void observed_vs_desired() {
    Model model;model.apply_inventory(inventory());model.set_selected("a",true);
    model.apply_status({{"engine",{{"active",true},{"all_streams_running",false},{"device_change_epoch",2},{"outputs",Json::array()}}},{"receivers",Json::array()}});
    check(model.active && !model.all_running && model.output("a").selected,"desired selection replaced by telemetry");
    rejects([&]{model.set_capture("loopback","b");},"active capture was changed");
    check(model.device_epoch==2,"device epoch missing");
}
void software_output_limit() {
    Model model;auto data=inventory();
    for(int i=0;i<9;++i)data["endpoints"].push_back({{"id","extra"+std::to_string(i)},{"name","Extra"+std::to_string(i)},{"direction","render"},{"active",true}});
    model.apply_inventory(data);for(int i=0;i<8;++i)model.set_selected("extra"+std::to_string(i),true);
    rejects([&]{model.set_selected("extra8",true);},"nine outputs accepted");
}
void recording_requires_explicit_selection() {
    Model model;model.set_capture("capture","");model.apply_inventory(inventory());
    check(model.capture_id.empty(),"recording endpoint was selected without user action");
    model.set_selected("a",true);rejects([&]{model.configuration();},"recording started without a source selection");
    model.set_capture("capture","record");check(model.configuration().at("capture").at("id")=="record","explicit recording source rejected");
}
void import_keeps_live_devices() {
    Model model;model.apply_inventory(inventory());model.set_selected("a",true);
    const auto config=model.configuration();model.load_profile(config);
    check(model.output("b").name=="Tai nghe B","import removed other currently connected outputs");
    check(!model.output("b").selected && model.output("a").selected,"import changed desired selection");
    auto empty=config;empty["outputs"]=Json::array();
    model.adopt_session(empty);check(model.selected_count()==0,"active session with last output removed cannot attach");
    rejects([&]{model.load_profile(empty);},"empty session accepted as startable imported configuration");
}
void command_ordering() {
    JobQueue queue;
    queue.push({JobKind::command,"master.configure","master",{},{{"gain",0.1}}});
    queue.push({JobKind::command,"session.stop"});
    queue.push({JobKind::command,"session.start"});
    queue.push({JobKind::command,"master.configure","master",{},{{"gain",0.2}}});
    queue.push({JobKind::command,"master.configure","master",{},{{"gain",0.3}}});
    check(queue.size()==4,"coalescing crossed a session boundary");
    check(queue.pop().params.at("gain")==0.1,"old session's adjustment overwritten");
    check(queue.pop().method=="session.stop" && queue.pop().method=="session.start","session command order changed");
    check(queue.pop().params.at("gain")==0.3,"last adjustment not coalesced");
}
void exit_and_restart_queue() {
    JobQueue queue;queue.push({JobKind::command,"receiver.open"});queue.push({JobKind::poll,{},"poll"});
    queue.push({JobKind::save,{},"save:a","a",{{"value",1}}});queue.push({JobKind::exit});
    queue.push({JobKind::command,"session.start"});
    check(queue.size()==2 && queue.pop().kind==JobKind::save && queue.pop().kind==JobKind::exit,"exit did not discard obsolete operations and preserve save");
    queue.exit_failed();queue.push({JobKind::poll,{},"poll"});check(queue.pop().kind==JobKind::poll,"failed exit prevented retry");
    queue.push({JobKind::command,"session.start"});queue.push({JobKind::save,{},"save:a","a"});queue.worker_restarted();
    check(queue.size()==1 && queue.pop().kind==JobKind::save,"stale command survived worker restart");
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={{"route guard",routes},{"missing device memory",disappearing_device},
        {"profile roundtrip/rollback",profile_roundtrip},{"backend configuration import",import_backend_configuration},
        {"receiver semantics",source_states_are_not_playback_proof},{"desired versus observed",observed_vs_desired},{"output limit",software_output_limit},
        {"explicit recording source",recording_requires_explicit_selection},{"import preserves devices",import_keeps_live_devices},
        {"command ordering",command_ordering},{"exit/restart queue",exit_and_restart_queue}};
    unsigned failures=0;for(const auto& [name,run]:tests){try{run();std::cout<<"PASS "<<name<<"\n";}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<"\n";}}
    return failures ? 1 : 0;
}
