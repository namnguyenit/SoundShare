#include "soundshare/audio.hpp"
#include <samplerate.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace soundshare {
AudioRing::AudioRing(std::size_t capacity_frames) : capacity_(capacity_frames) {
    if (capacity_frames == 0 || capacity_frames > sample_rate * 10ULL)
        throw std::invalid_argument("audio queue capacity must be between 1 frame and 10 seconds");
    data_.resize(capacity_frames * channels);
}
std::size_t AudioRing::write(std::span<const float> samples) noexcept {
    if (samples.size() % channels) return 0;
    const auto w = written_.load(std::memory_order_relaxed);
    const auto r = read_.load(std::memory_order_acquire);
    const auto count = std::min(samples.size() / channels, capacity_ - static_cast<std::size_t>(w-r));
    if(!count) return 0;
    const auto offset=static_cast<std::size_t>(w%capacity_);
    const auto first=std::min(count,capacity_-offset);
    std::copy_n(samples.data(),first*channels,data_.data()+offset*channels);
    std::copy_n(samples.data()+first*channels,(count-first)*channels,data_.data());
    written_.store(w+count, std::memory_order_release);
    return count;
}
std::size_t AudioRing::peek(std::span<float> samples) const noexcept {
    const auto r = read_.load(std::memory_order_relaxed);
    const auto w = written_.load(std::memory_order_acquire);
    const auto count = std::min(samples.size()/channels, static_cast<std::size_t>(w-r));
    if(!count) return 0;
    const auto offset=static_cast<std::size_t>(r%capacity_);
    const auto first=std::min(count,capacity_-offset);
    std::copy_n(data_.data()+offset*channels,first*channels,samples.data());
    std::copy_n(data_.data(),(count-first)*channels,samples.data()+first*channels);
    return count;
}
std::size_t AudioRing::consume(std::size_t frames) noexcept {
    const auto r = read_.load(std::memory_order_relaxed);
    const auto w = written_.load(std::memory_order_acquire);
    const auto count = std::min(frames, static_cast<std::size_t>(w-r));
    read_.store(r+count, std::memory_order_release);
    return count;
}
void AudioRing::discard() noexcept {
    read_.store(written_.load(std::memory_order_acquire), std::memory_order_release);
}
std::size_t AudioRing::available() const noexcept {
    const auto r = read_.load(std::memory_order_acquire);
    const auto w = written_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(std::min<std::uint64_t>(w-r, capacity_));
}
void GainRamp::reset(float value) noexcept { current_=target_=value; remaining_=0; step_=0; }
void GainRamp::target(float value, unsigned frames) noexcept {
    target_ = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    remaining_ = frames;
    if (!frames) current_ = target_;
    step_ = frames ? (target_-current_)/static_cast<float>(frames) : 0;
}
void GainRamp::apply(std::span<float> stereo) noexcept {
    for (std::size_t f=0; f<stereo.size()/channels; ++f) {
        if (remaining_) { current_+=step_; if (!--remaining_) current_=target_; }
        for (unsigned c=0;c<channels;++c) stereo[f*channels+c]*=current_;
    }
}
void ClockRate::reset() noexcept { seeded_=valid_=false; filtered_=1; }
bool ClockRate::observe(std::uint64_t position, std::uint64_t time, double frequency) noexcept {
    if (!(frequency>0) || !std::isfinite(frequency)) { reset(); return false; }
    if (!seeded_) { position_=position; time_=time; seeded_=true; return false; }
    if (time<=time_ || position<position_) { reset(); return false; }
    const double dt=static_cast<double>(time-time_)/1e7;
    if (dt<0.5) return valid_;
    const double observed=static_cast<double>(position-position_)/(frequency*dt);
    position_=position; time_=time;
    if (!std::isfinite(observed) || std::abs(observed-1)>0.002 || dt>5) {
        valid_=false; filtered_=1; return false;
    }
    filtered_ = valid_ ? filtered_+(observed-filtered_)*std::min(dt/10,1.0) : observed;
    valid_=true; return true;
}
DriftController::DriftController(double limit_ppm) : limit_(limit_ppm*1e-6) {
    if (!std::isfinite(limit_ppm) || limit_ppm<=0 || limit_ppm>2000)
        throw std::invalid_argument("invalid drift limit");
}
void DriftController::reset() noexcept { integral_=filtered_error_=0; ratio_=1; }
double DriftController::update(double queued, double target, double dt, double clock_ratio) noexcept {
    if (!std::isfinite(queued) || !std::isfinite(target) || !std::isfinite(dt) || target<=0 || dt<=0)
        return ratio_;
    dt=std::min(dt,0.25);
    const double error=(queued-target)/sample_rate;
    filtered_error_+=(error-filtered_error_)*std::min(dt/2,1.0);
    integral_=std::clamp(integral_+filtered_error_*dt*0.0001,-limit_,limit_);
    const double clock=std::isfinite(clock_ratio) ? std::clamp(clock_ratio,1-limit_,1+limit_) : 1;
    // SRC ratio is output/input. A high queue requires consuming MORE input, hence a SMALLER ratio.
    const double requested=std::clamp(clock-filtered_error_*0.01-integral_,1-limit_,1+limit_);
    ratio_+=std::clamp(requested-ratio_,-dt*20e-6,dt*20e-6);
    return ratio_;
}
struct Resampler::State { SRC_STATE* handle{}; };
Resampler::Resampler() : state_(std::make_unique<State>()) {
    int error=0;
    state_->handle=src_new(SRC_SINC_FASTEST, channels, &error);
    if (!state_->handle) throw std::runtime_error(src_strerror(error));
}
Resampler::~Resampler() { if (state_->handle) src_delete(state_->handle); }
void Resampler::reset() noexcept { src_reset(state_->handle); buffered_input_frames_=0; }
ResampleResult Resampler::process(std::span<const float> input, std::span<float> output, double ratio) {
    if (input.size()%channels || output.size()%channels || !std::isfinite(ratio) || !src_is_valid_ratio(ratio))
        throw std::invalid_argument("invalid resampler input or ratio");
    SRC_DATA data{};
    data.data_in=input.data(); data.data_out=output.data();
    data.input_frames=static_cast<long>(input.size()/channels);
    data.output_frames=static_cast<long>(output.size()/channels);
    data.src_ratio=ratio;
    const int error=src_process(state_->handle,&data);
    if (error) throw std::runtime_error(src_strerror(error));
    buffered_input_frames_=std::max(0.0,buffered_input_frames_+static_cast<double>(data.input_frames_used)-
        static_cast<double>(data.output_frames_gen)/ratio);
    return {static_cast<std::size_t>(data.input_frames_used),static_cast<std::size_t>(data.output_frames_gen)};
}
BranchSlot::BranchSlot() : queue(sample_rate*2ULL) {}
void FanoutBus::publish(std::span<const float> input) noexcept {
    captured_frames.fetch_add(input.size()/channels,std::memory_order_relaxed);
    for (auto& slot:slots_) if (slot.enabled.load(std::memory_order_acquire)) {
        const auto wrote=slot.queue.write(input);
        if (wrote<input.size()/channels) {
            slot.dropped_frames.fetch_add(input.size()/channels-wrote,std::memory_order_relaxed);
            slot.resync.store(true,std::memory_order_release);
        }
    }
}
void FanoutBus::discontinuity() noexcept {
    discontinuities.fetch_add(1,std::memory_order_relaxed);
    source_clock_valid.store(false);
    for (auto& slot:slots_) if (slot.enabled.load()) slot.resync.store(true);
}
BranchProcessor::BranchProcessor(BranchSlot& slot,unsigned buffer_ms)
    : slot_(slot),buffer_ms_(buffer_ms),input_(4096*channels),last_delay_(slot.delay_ms.load()) {
    if (buffer_ms<20 || buffer_ms>500) throw std::invalid_argument("buffer_ms must be 20..500");
}
void BranchProcessor::restart(bool discard) {
    if (discard) slot_.queue.discard();
    resampler_.reset(); drift_.reset(); gain_.reset(); primed_=false; last_target_=-1;
}
void BranchProcessor::render(std::span<float> output,double source_rate,bool source_valid,float master_gain) {
    if (output.size()%channels) throw std::invalid_argument("output must contain complete stereo frames");
    std::fill(output.begin(),output.end(),0.0f);
    slot_.peak.store(0,std::memory_order_relaxed);
    const auto frames=output.size()/channels;
    const auto delay=slot_.delay_ms.load();
    if (delay>1000) throw std::invalid_argument("delay_ms must be 0..1000");
    if (slot_.resync.exchange(false) || delay!=last_delay_) {
        restart(true); last_delay_=delay; slot_.recoveries.fetch_add(1);
    }
    const auto target=static_cast<std::size_t>(buffer_ms_+delay)*sample_rate/1000;
    if (!primed_) {
        if (slot_.queue.available()<target+frames+64) return;
        // Align to the requested queue depth after startup/recovery, discarding only during priming.
        const auto surplus=slot_.queue.available()-(target+frames+64);
        slot_.queue.consume(surplus);
        primed_=true;
    }
    double clock_ratio=1;
    if (source_valid && slot_.device_clock_valid.load() && source_rate>0)
        clock_ratio=slot_.device_clock_ratio.load()/source_rate;
    const double ratio=drift_.update(static_cast<double>(slot_.queue.available())+resampler_.buffered_input_frames(),
        static_cast<double>(target+frames+64),static_cast<double>(frames)/sample_rate,clock_ratio);
    slot_.correction_ppm.store(drift_.correction_ppm());
    std::size_t produced=0;
    for (unsigned iteration=0;produced<frames && iteration<32;++iteration) {
        // libsamplerate may accept substantially more input than it produces. Account
        // for its internal reservoir instead of silently hiding tens of ms from drift control.
        const auto budget=static_cast<std::size_t>(std::max(1.0,std::ceil(
            static_cast<double>(frames-produced)/ratio+48-resampler_.buffered_input_frames())));
        const auto limit=std::min(budget,input_.size()/channels);
        const auto got=slot_.queue.peek(std::span<float>(input_.data(),limit*channels));
        // Sanitize before the stateful filter: one NaN otherwise contaminates future blocks.
        for(auto& value:std::span<float>(input_.data(),got*channels))
            value=std::isfinite(value) ? std::clamp(value,-8.0f,8.0f) : 0.0f;
        const auto result=resampler_.process(std::span<const float>(input_.data(),got*channels),
            output.subspan(produced*channels),ratio);
        slot_.queue.consume(result.consumed); produced+=result.produced;
        if (!result.consumed && !result.produced) break;
    }
    const float desired=slot_.gain.load()*std::clamp(std::isfinite(master_gain) ? master_gain : 0.0f,0.0f,1.0f);
    if (desired!=last_target_) { gain_.target(desired, sample_rate/100); last_target_=desired; }
    gain_.apply(output);
    limit_samples(output);
    if (produced<frames) {
        // Fade the last valid section, then re-prime. This remains a reported dropout.
        const auto fade=std::min<std::size_t>(produced, sample_rate/200);
        for (std::size_t n=0;n<fade;++n) {
            const float g=static_cast<float>(fade-n-1)/static_cast<float>(fade);
            for(unsigned c=0;c<channels;++c) output[(produced-fade+n)*channels+c]*=g;
        }
        slot_.underruns.fetch_add(1); restart(false);
    }
    float peak=0;
    for(float value:output) peak=std::max(peak,std::abs(value));
    slot_.peak.store(peak);
    slot_.rendered_frames.fetch_add(frames);
}
void mix_add(std::span<const float> input,std::span<float> output,float gain) {
    if(input.size()!=output.size() || !std::isfinite(gain)) throw std::invalid_argument("invalid mixer input");
    for(std::size_t n=0;n<input.size();++n) output[n]+=input[n]*gain;
}
void limit_samples(std::span<float> samples) noexcept {
    for(float& value:samples) value=std::isfinite(value) ? std::clamp(value,-1.0f,1.0f) : 0.0f;
}
} // namespace soundshare
