#include "soundshare/engine.hpp"
#include "soundshare/audio.hpp"
#include "soundshare/platform.hpp"
#include <avrt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>
#include <thread>

namespace soundshare {
void validate_config(const EngineConfig& config) {
    if(config.capture_id.empty() || config.capture_id.size()>4096 || config.capture_id.find('\0')!=std::string::npos)
        throw BackendError("invalid_config","invalid capture endpoint ID");
    if(config.bridge_render_id.size()>4096 || config.bridge_render_id.find('\0')!=std::string::npos)
        throw BackendError("invalid_config","invalid bridge render ID");
    if(config.buffer_ms<20 || config.buffer_ms>500) throw BackendError("invalid_config","buffer_ms must be 20..500");
    if(!std::isfinite(config.master_gain) || config.master_gain<0 || config.master_gain>1)
        throw BackendError("invalid_config","master_gain must be 0..1");
    if(config.outputs.empty() || config.outputs.size()>max_outputs) throw BackendError("invalid_config","choose between 1 and 8 outputs");
    std::set<std::string> ids;
    for(const auto& output:config.outputs) {
        if(output.id.empty() || output.id.size()>4096 || output.id.find('\0')!=std::string::npos || !ids.insert(output.id).second)
            throw BackendError("invalid_config","empty, oversized or duplicate output ID");
        if(!std::isfinite(output.gain) || output.gain<0 || output.gain>1 || output.delay_ms>1000)
            throw BackendError("invalid_config","gain must be 0..1 and delay_ms 0..1000");
        if((config.loopback && output.id==config.capture_id) ||
           (!config.bridge_render_id.empty() && output.id==config.bridge_render_id))
            throw BackendError("feedback_route","capture/bridge render endpoint cannot also be an output");
    }
}
namespace {
struct AudioTask {
    DWORD index{}; HANDLE task=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
    AudioTask() { if(task) AvSetMmThreadPriority(task,AVRT_PRIORITY_HIGH); }
    ~AudioTask() { if(task) AvRevertMmThreadCharacteristics(task); }
};
struct Health {
    std::atomic<const char*> state{"stopped"};
    std::atomic<unsigned> retries{0};
    std::atomic<long> hresult{0};
    std::atomic<double> latency_ms{0};
    mutable std::mutex mutex;
    std::string error;
    std::vector<std::string> recent_errors;
    void fail(const std::exception& problem) {
        std::lock_guard lock(mutex); error=problem.what();
        if(recent_errors.size()>=16) recent_errors.erase(recent_errors.begin());
        recent_errors.push_back(std::to_string(qpc_100ns())+": "+error);
        const auto* backend=dynamic_cast<const BackendError*>(&problem);
        hresult.store(backend ? backend->hresult : E_FAIL);
        state.store("retry_wait");
    }
    void clear() { std::lock_guard lock(mutex); error.clear(); hresult.store(0); }
    StreamStatus snapshot(const std::string& id) const {
        StreamStatus result; result.id=id; result.state=state.load(); result.retries=retries.load();
        result.hresult=hresult.load(); result.stream_latency_ms=latency_ms.load();
        std::lock_guard lock(mutex); result.error=error; result.recent_errors=recent_errors; return result;
    }
};
class Notifications final : public IMMNotificationClient {
public:
    std::atomic<std::uint64_t> epoch{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IMMNotificationClient)) {
            *out=static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto left=--references_; if(!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override { ++epoch; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { ++epoch; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { ++epoch; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow,ERole,LPCWSTR) override { ++epoch; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override { ++epoch; return S_OK; }
private:
    std::atomic<ULONG> references_{1};
};
}
struct AudioEngine::Impl {
    struct OutputWorker {
        OutputConfig config;
        Health health;
        Handle stop=make_event(true);
        std::thread thread;
        std::size_t slot{};
        void join() { SetEvent(stop.get()); if(thread.joinable()) thread.join(); }
        ~OutputWorker() { join(); }
    };
    mutable std::mutex control;
    bool active=false;
    EngineConfig config;
    std::unique_ptr<FanoutBus> bus=std::make_unique<FanoutBus>();
    Health capture_health;
    Handle capture_stop=make_event(true);
    std::thread capture_thread;
    std::array<std::unique_ptr<OutputWorker>,max_outputs> outputs;
    std::atomic<std::uint64_t> timestamp_errors{0};
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<Notifications> notifications;
    Impl() {
        check_hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"create audio notification enumerator");
        notifications.Attach(new Notifications());
        check_hr(enumerator->RegisterEndpointNotificationCallback(notifications.Get()),"register device notifications");
    }
    ~Impl() { if(enumerator && notifications) enumerator->UnregisterEndpointNotificationCallback(notifications.Get()); }

    void run_capture() noexcept {
        try {
            ComApartment apartment;
            unsigned failures=0;
            while(WaitForSingleObject(capture_stop.get(),0)!=WAIT_OBJECT_0) {
                const auto opened_at=qpc_100ns();
                try {
                    capture_health.state.store("opening");
                    auto audio=open_audio(config.capture_id,true,config.loopback);
                    capture_health.latency_ms.store(static_cast<double>(audio.latency)/10000);
                    ComPtr<IAudioCaptureClient> capture;
                    check_hr(audio.client->GetService(IID_PPV_ARGS(&capture)),"capture service");
                    std::vector<float> silence(sample_rate/10*channels,0);
                    ClockRate source_clock;
                    AudioTask scheduling;
                    check_hr(audio.client->Start(),"start capture");
                    capture_health.clear(); capture_health.state.store("running");
                    auto last_packet=qpc_100ns(), last_publish=last_packet;
                    auto last_tick=last_packet;
                    bool received=false;
                    HANDLE events[]={capture_stop.get(),audio.event.get()};
                    while(true) {
                        const auto waited=WaitForMultipleObjects(2,events,FALSE,10);
                        if(waited==WAIT_OBJECT_0) break;
                        if(waited==WAIT_FAILED) check_hr(HRESULT_FROM_WIN32(GetLastError()),"wait capture event");
                        const auto tick=qpc_100ns();
                        if(tick-last_tick>20000000ULL) {
                            // Sleep/resume or a long scheduler stall is not a backlog of
                            // silence to replay. Drop stale timing and re-prime bounded queues.
                            bus->discontinuity(); source_clock.reset();
                            last_packet=last_publish=tick;
                        }
                        last_tick=tick;
                        UINT32 next=0;
                        check_hr(capture->GetNextPacketSize(&next),"next capture packet");
                        bool got_packet=false;
                        while(next) {
                            BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0; UINT64 position=0,time=0;
                            check_hr(capture->GetBuffer(&data,&frames,&flags,&position,&time),"capture buffer");
                            if((flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) && received) {
                                bus->discontinuity(); source_clock.reset();
                            }
                            if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
                                ++timestamp_errors; source_clock.reset(); bus->source_clock_valid.store(false);
                            } else {
                                source_clock.observe(position,time,sample_rate);
                                bus->source_clock_ratio.store(source_clock.relative());
                                bus->source_clock_valid.store(source_clock.valid());
                            }
                            if((flags&AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                                auto remaining=static_cast<std::size_t>(frames);
                                while(remaining) {
                                    const auto count=std::min(remaining,silence.size()/channels);
                                    bus->publish(std::span<const float>(silence.data(),count*channels)); remaining-=count;
                                }
                            } else bus->publish(std::span<const float>(reinterpret_cast<const float*>(data),static_cast<std::size_t>(frames)*channels));
                            check_hr(capture->ReleaseBuffer(frames),"release capture buffer");
                            received=true; got_packet=true;
                            check_hr(capture->GetNextPacketSize(&next),"next capture packet");
                            if(WaitForSingleObject(capture_stop.get(),0)==WAIT_OBJECT_0) break;
                        }
                        const auto now=qpc_100ns();
                        if(got_packet) { last_packet=last_publish=now; }
                        else if(now-last_packet>=500000) {
                            // Loopback need not deliver packets while the system is idle. Keep a timed
                            // silent bus; count this separately, never label it a successful RF packet.
                            const auto count=std::min<std::size_t>((now-last_publish)*sample_rate/10000000ULL,silence.size()/channels);
                            if(count) {
                                bus->publish(std::span<const float>(silence.data(),count*channels));
                                bus->idle_silence_frames.fetch_add(count);
                                last_publish+=count*10000000ULL/sample_rate;
                                bus->source_clock_valid.store(false);
                            }
                        }
                    }
                    audio.client->Stop(); break;
                } catch(const std::exception& error) {
                    capture_health.fail(error); bus->discontinuity();
                    if(qpc_100ns()-opened_at>300000000ULL) failures=0;
                    ++failures; capture_health.retries.store(failures);
                    if(failures>=6) { capture_health.state.store("failed"); return; }
                    if(WaitForSingleObject(capture_stop.get(),std::min(8000U,500U<<failures))==WAIT_OBJECT_0) break;
                }
            }
            capture_health.state.store("stopped");
        } catch(const std::exception& error) { capture_health.fail(error); capture_health.state.store("failed"); }
        catch(...) { capture_health.state.store("failed"); }
    }
    void run_output(OutputWorker& worker) noexcept {
        auto& slot=bus->slot(worker.slot);
        try {
            ComApartment apartment;
            unsigned failures=0;
            while(WaitForSingleObject(worker.stop.get(),0)!=WAIT_OBJECT_0) {
                const auto opened_at=qpc_100ns();
                try {
                    worker.health.state.store("opening");
                    auto audio=open_audio(worker.config.id,false,false);
                    worker.health.latency_ms.store(static_cast<double>(audio.latency)/10000);
                    ComPtr<IAudioRenderClient> render;
                    check_hr(audio.client->GetService(IID_PPV_ARGS(&render)),"render service");
                    ComPtr<IAudioClock> clock;
                    check_hr(audio.client->GetService(IID_PPV_ARGS(&clock)),"audio clock service");
                    UINT64 frequency=0; check_hr(clock->GetFrequency(&frequency),"audio clock frequency");
                    Handle clock_stop=make_event(true);
                    struct ClockThread {
                        HANDLE stop; std::thread thread;
                        ~ClockThread() { SetEvent(stop); if(thread.joinable()) thread.join(); }
                    } clock_thread{clock_stop.get(),std::thread([&,raw=clock.Get(),frequency] {
                        try {
                            ComApartment clock_apartment; ClockRate rate;
                            while(WaitForSingleObject(clock_stop.get(),200)==WAIT_TIMEOUT) {
                                UINT64 position=0,time=0;
                                if(raw->GetPosition(&position,&time)==S_OK) {
                                    rate.observe(position,time,static_cast<double>(frequency));
                                    slot.device_clock_ratio.store(rate.relative()); slot.device_clock_valid.store(rate.valid());
                                } else { rate.reset(); slot.device_clock_valid.store(false); }
                            }
                        } catch(...) { slot.device_clock_valid.store(false); }
                    })};
                    slot.queue.discard(); slot.resync.store(false); slot.device_clock_valid.store(false);
                    BranchProcessor processor(slot,config.buffer_ms);
                    BYTE* prefill=nullptr;
                    check_hr(render->GetBuffer(audio.frames,&prefill),"prefill render buffer");
                    check_hr(render->ReleaseBuffer(audio.frames,AUDCLNT_BUFFERFLAGS_SILENT),"release prefill");
                    AudioTask scheduling;
                    check_hr(audio.client->Start(),"start render");
                    worker.health.clear(); worker.health.state.store("running");
                    HANDLE events[]={worker.stop.get(),audio.event.get()};
                    bool stopping=false; std::uint64_t deadline=0;
                    while(true) {
                        DWORD waited;
                        if(stopping) waited=WaitForSingleObject(audio.event.get(),20);
                        else waited=WaitForMultipleObjects(2,events,FALSE,1000);
                        if(!stopping && waited==WAIT_OBJECT_0) {
                            stopping=true; slot.gain.store(0);
                            deadline=qpc_100ns()+std::max<std::uint64_t>(1000000ULL,static_cast<std::uint64_t>(audio.frames)*10000000ULL/sample_rate+200000ULL);
                        }
                        if(stopping && qpc_100ns()>=deadline) break;
                        if(waited==WAIT_FAILED) check_hr(HRESULT_FROM_WIN32(GetLastError()),"wait render event");
                        UINT32 padding=0;
                        check_hr(audio.client->GetCurrentPadding(&padding),"render padding");
                        if(padding>audio.frames) throw BackendError("invalid_audio_state","render padding exceeds buffer");
                        const UINT32 available=audio.frames-padding;
                        if(!available) continue;
                        BYTE* data=nullptr;
                        check_hr(render->GetBuffer(available,&data),"render buffer");
                        try {
                            processor.render(std::span<float>(reinterpret_cast<float*>(data),static_cast<std::size_t>(available)*channels),
                                bus->source_clock_ratio.load(),bus->source_clock_valid.load(),bus->master_gain.load());
                        } catch(...) { render->ReleaseBuffer(available,AUDCLNT_BUFFERFLAGS_SILENT); throw; }
                        check_hr(render->ReleaseBuffer(available,0),"release render buffer");
                    }
                    audio.client->Stop(); break;
                } catch(const std::exception& error) {
                    worker.health.fail(error); slot.device_clock_valid.store(false); slot.queue.discard();
                    if(qpc_100ns()-opened_at>300000000ULL) failures=0;
                    ++failures; worker.health.retries.store(failures);
                    if(failures>=6) { worker.health.state.store("failed"); slot.enabled.store(false); return; }
                    slot.recoveries.fetch_add(1);
                    if(WaitForSingleObject(worker.stop.get(),std::min(8000U,500U<<failures))==WAIT_OBJECT_0) break;
                }
            }
            worker.health.state.store("stopped");
        } catch(const std::exception& error) { worker.health.fail(error); worker.health.state.store("failed"); }
        catch(...) { worker.health.state.store("failed"); }
        slot.enabled.store(false);
    }
    void add(const OutputConfig& output) {
        auto candidate=config;
        candidate.outputs.push_back(output); validate_config(candidate);
        validate_endpoint(output.id,eRender);
        std::size_t index=0; while(index<outputs.size() && outputs[index]) ++index;
        if(index==outputs.size()) throw BackendError("capacity_exceeded","no free output slots");
        auto worker=std::make_unique<OutputWorker>(); worker->slot=index; worker->config=output;
        auto& slot=bus->slot(index);
        slot.dropped_frames.store(0); slot.underruns.store(0); slot.recoveries.store(0); slot.rendered_frames.store(0);
        slot.correction_ppm.store(0); slot.peak.store(0); slot.device_clock_valid.store(false);
        slot.gain.store(output.muted ? 0 : output.gain); slot.delay_ms.store(output.delay_ms);
        auto* raw=worker.get();
        slot.enabled.store(true);
        try { worker->thread=std::thread([this,raw] { run_output(*raw); }); }
        catch(...) { slot.enabled.store(false); throw; }
        outputs[index]=std::move(worker); config.outputs.push_back(output);
    }
    void stop_locked() {
        SetEvent(capture_stop.get());
        // Signal every output before joining, so the fade/drain windows overlap.
        for(const auto& output:outputs)if(output)SetEvent(output->stop.get());
        if(capture_thread.joinable()) capture_thread.join();
        for(std::size_t i=0;i<outputs.size();++i) if(outputs[i]) {
            bus->slot(i).enabled.store(false); outputs[i]->join(); outputs[i].reset();
        }
        active=false; capture_health.state.store("stopped");
    }
};
AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { stop(); }
void AudioEngine::start(const EngineConfig& config) {
    validate_config(config);
    std::lock_guard lock(impl_->control);
    if(impl_->active) throw BackendError("already_running","stop the current session before replacing its configuration");
    validate_endpoint(config.capture_id,config.loopback ? eRender : eCapture);
    if(!config.bridge_render_id.empty()) validate_endpoint(config.bridge_render_id,eRender);
    for(const auto& output:config.outputs) validate_endpoint(output.id,eRender);
    impl_->bus=std::make_unique<FanoutBus>(); impl_->timestamp_errors.store(0);
    impl_->bus->master_gain.store(config.master_gain);
    impl_->config=config; impl_->config.outputs.clear();
    ResetEvent(impl_->capture_stop.get()); impl_->capture_health.clear(); impl_->capture_health.retries.store(0);
    try {
        for(const auto& output:config.outputs) impl_->add(output);
        impl_->capture_thread=std::thread([this] { impl_->run_capture(); });
        impl_->active=true;
    } catch(...) { impl_->stop_locked(); throw; }
}
void AudioEngine::stop() { std::lock_guard lock(impl_->control); impl_->stop_locked(); }
void AudioEngine::add_output(const OutputConfig& output) {
    std::lock_guard lock(impl_->control);
    if(!impl_->active) throw BackendError("not_running","session is not active");
    impl_->add(output);
}
void AudioEngine::remove_output(const std::string& id) {
    std::lock_guard lock(impl_->control);
    for(std::size_t i=0;i<impl_->outputs.size();++i) if(impl_->outputs[i] && impl_->outputs[i]->config.id==id) {
        impl_->bus->slot(i).enabled.store(false);
        impl_->outputs[i]->join(); impl_->outputs[i].reset();
        std::erase_if(impl_->config.outputs,[&](const OutputConfig& output) { return output.id==id; }); return;
    }
    // Removal is idempotent: rapid GUI toggles may arrive before telemetry catches up.
}
void AudioEngine::configure_output(const OutputConfig& output) {
    std::lock_guard lock(impl_->control);
    if(!impl_->active) throw BackendError("not_running","session is not active");
    auto candidate=impl_->config;
    auto found=std::find_if(candidate.outputs.begin(),candidate.outputs.end(),[&](const auto& item) { return item.id==output.id; });
    if(found==candidate.outputs.end()) throw BackendError("not_found","output is not part of this session");
    *found=output; validate_config(candidate);
    for(auto& worker:impl_->outputs) if(worker && worker->config.id==output.id) {
        // The worker reads its immutable ID during recovery. Do not assign that string
        // concurrently with the audio thread; live controls are delivered atomically.
        worker->config.gain=output.gain;
        worker->config.muted=output.muted;
        worker->config.delay_ms=output.delay_ms;
        auto& slot=impl_->bus->slot(worker->slot);
        slot.gain.store(output.muted ? 0 : output.gain); slot.delay_ms.store(output.delay_ms);
    }
    auto configured=std::find_if(impl_->config.outputs.begin(),impl_->config.outputs.end(),[&](const auto& item) { return item.id==output.id; });
    *configured=output;
}
EngineConfig AudioEngine::configuration() const { std::lock_guard lock(impl_->control); return impl_->config; }
void AudioEngine::set_master_gain(float gain) {
    if(!std::isfinite(gain) || gain<0 || gain>1) throw BackendError("invalid_argument","gain must be 0..1");
    std::lock_guard lock(impl_->control); impl_->bus->master_gain.store(gain); impl_->config.master_gain=gain;
}
EngineStatus AudioEngine::status() const {
    std::lock_guard lock(impl_->control); EngineStatus result;
    result.active=impl_->active; result.capture=impl_->capture_health.snapshot(impl_->config.capture_id);
    result.master_gain=impl_->bus->master_gain.load();
    result.capture.frames=impl_->bus->captured_frames.load();
    result.capture.clock_ratio=impl_->bus->source_clock_ratio.load();
    result.capture.clock_valid=impl_->bus->source_clock_valid.load();
    result.discontinuities=impl_->bus->discontinuities.load(); result.timestamp_errors=impl_->timestamp_errors.load();
    result.idle_silence_frames=impl_->bus->idle_silence_frames.load(); result.device_change_epoch=impl_->notifications->epoch.load();
    result.all_streams_running=result.active && result.capture.state=="running";
    bool has_output=false;
    for(const auto& worker:impl_->outputs) if(worker) {
        const auto& slot=impl_->bus->slot(worker->slot); auto status=worker->health.snapshot(worker->config.id);
        status.frames=slot.rendered_frames.load(); status.queued_frames=slot.queue.available(); status.dropped_frames=slot.dropped_frames.load();
        status.underruns=slot.underruns.load(); status.recoveries=slot.recoveries.load(); status.drift_ppm=slot.correction_ppm.load();
        status.clock_ratio=slot.device_clock_ratio.load(); status.clock_valid=slot.device_clock_valid.load(); status.peak=slot.peak.load();
        result.all_streams_running=result.all_streams_running && status.state=="running"; has_output=true;
        result.outputs.push_back(std::move(status));
    }
    result.all_streams_running=result.all_streams_running && has_output; return result;
}
} // namespace soundshare
