#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace soundshare {
inline constexpr unsigned sample_rate = 48000;
inline constexpr unsigned channels = 2;
inline constexpr unsigned max_outputs = 8;

// Exactly one producer and one consumer. The producer never moves the read cursor.
class AudioRing {
public:
    explicit AudioRing(std::size_t capacity_frames);
    std::size_t write(std::span<const float> samples) noexcept;
    std::size_t peek(std::span<float> samples) const noexcept;
    std::size_t consume(std::size_t frames) noexcept;
    void discard() noexcept;
    std::size_t available() const noexcept;
    std::size_t capacity() const noexcept { return capacity_; }
private:
    std::size_t capacity_;
    std::vector<float> data_;
    alignas(64) std::atomic<std::uint64_t> written_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
};

class GainRamp {
public:
    void reset(float value = 0.0f) noexcept;
    void target(float value, unsigned frames) noexcept;
    void apply(std::span<float> stereo) noexcept;
    float value() const noexcept { return current_; }
private:
    float current_{0}, target_{0}, step_{0};
    unsigned remaining_{0};
};

// Estimates clock frequency relative to nominal from positions and 100 ns QPC timestamps.
class ClockRate {
public:
    void reset() noexcept;
    bool observe(std::uint64_t position, std::uint64_t qpc_100ns, double frequency) noexcept;
    bool valid() const noexcept { return valid_; }
    double relative() const noexcept { return filtered_; }
private:
    std::uint64_t position_{0}, time_{0};
    double filtered_{1};
    bool seeded_{false}, valid_{false};
};

class DriftController {
public:
    explicit DriftController(double limit_ppm = 300.0);
    double update(double queued_frames, double target_frames, double elapsed_seconds,
                  double clock_ratio = 1.0) noexcept;
    void reset() noexcept;
    double correction_ppm() const noexcept { return (ratio_ - 1.0) * 1e6; }
private:
    double limit_, integral_{0}, filtered_error_{0}, ratio_{1};
};

struct ResampleResult { std::size_t consumed{}, produced{}; };
class Resampler {
public:
    Resampler();
    ~Resampler();
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;
    ResampleResult process(std::span<const float> input, std::span<float> output, double ratio);
    void reset() noexcept;
    double buffered_input_frames() const noexcept { return buffered_input_frames_; }
private:
    struct State;
    std::unique_ptr<State> state_;
    double buffered_input_frames_{0};
};

struct BranchSlot {
    BranchSlot();
    AudioRing queue;
    std::atomic<bool> enabled{false}, resync{false};
    std::atomic<float> gain{1};
    std::atomic<unsigned> delay_ms{0};
    std::atomic<std::uint64_t> dropped_frames{0}, underruns{0}, recoveries{0}, rendered_frames{0};
    std::atomic<double> correction_ppm{0}, device_clock_ratio{1};
    std::atomic<bool> device_clock_valid{false};
    std::atomic<float> peak{0};
};

class FanoutBus {
public:
    void publish(std::span<const float> samples) noexcept;
    void discontinuity() noexcept;
    BranchSlot& slot(std::size_t index) { return slots_.at(index); }
    const BranchSlot& slot(std::size_t index) const { return slots_.at(index); }
    std::atomic<double> source_clock_ratio{1};
    std::atomic<float> master_gain{1};
    std::atomic<bool> source_clock_valid{false};
    std::atomic<std::uint64_t> captured_frames{0}, discontinuities{0}, idle_silence_frames{0};
private:
    std::array<BranchSlot, max_outputs> slots_;
};

// Consumer-side processing, including streaming SRC, priming and click-free gain.
// All scratch and SRC state is allocated before the device starts.
class BranchProcessor {
public:
    BranchProcessor(BranchSlot& slot, unsigned buffer_ms);
    void render(std::span<float> output, double source_clock_ratio = 1.0,
                bool source_clock_valid = false, float master_gain = 1.0f);
    bool primed() const noexcept { return primed_; }
private:
    void restart(bool discard);
    BranchSlot& slot_;
    unsigned buffer_ms_;
    std::vector<float> input_;
    Resampler resampler_;
    DriftController drift_;
    GainRamp gain_;
    bool primed_{false};
    float last_target_{-1};
    unsigned last_delay_{0};
};

void mix_add(std::span<const float> input, std::span<float> output, float gain);
void limit_samples(std::span<float> samples) noexcept;
} // namespace soundshare
