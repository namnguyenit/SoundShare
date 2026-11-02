#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace soundshare {
struct OutputConfig {
    std::string id;
    float gain{1};
    bool muted{false};
    unsigned delay_ms{0};
};
struct EngineConfig {
    std::string capture_id;
    bool loopback{false};
    std::string bridge_render_id;
    unsigned buffer_ms{120};
    float master_gain{1};
    std::vector<OutputConfig> outputs;
};
void validate_config(const EngineConfig& config);
struct StreamStatus {
    std::string id,state,error;
    unsigned retries{};
    long hresult{};
    double stream_latency_ms{};
    std::uint64_t frames{},queued_frames{},dropped_frames{},underruns{},recoveries{};
    double drift_ppm{},clock_ratio{1};
    bool clock_valid{};
    float peak{};
    std::vector<std::string> recent_errors;
};
struct EngineStatus {
    bool active{},all_streams_running{};
    StreamStatus capture;
    std::vector<StreamStatus> outputs;
    std::uint64_t discontinuities{},timestamp_errors{},idle_silence_frames{},device_change_epoch{};
    float master_gain{1};
};
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    void start(const EngineConfig& config);
    void stop();
    void add_output(const OutputConfig& output);
    void remove_output(const std::string& id);
    void configure_output(const OutputConfig& output);
    void set_master_gain(float gain);
    EngineStatus status() const;
    EngineConfig configuration() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace soundshare
