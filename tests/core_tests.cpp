#include "soundshare/audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace soundshare;
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static void near(double actual,double expected,double tolerance,const char* message) {
    if(std::abs(actual-expected)>tolerance) throw std::runtime_error(message);
}
static void ring_wrap() {
    AudioRing ring(5); std::array<float,8> first={1,2,3,4,5,6,7,8};
    require(ring.write(first)==4,"initial write"); require(ring.consume(3)==3,"consume");
    require(ring.write(first)==4,"wrapped write"); require(ring.write(first)==0,"full queue must not overwrite");
    std::array<float,10> output{}; require(ring.peek(output)==5,"peek frame count");
    require(output==std::array<float,10>{7,8,1,2,3,4,5,6,7,8},"wrapped stereo ordering");
    require(ring.peek(output)==5,"peek cannot consume"); ring.discard(); require(ring.available()==0,"discard");
    std::array<float,3> incomplete{}; require(ring.write(incomplete)==0,"reject partial stereo frame");
}
static void ring_concurrent() {
    AudioRing ring(257); constexpr unsigned count=200000; std::atomic<bool> failed{false};
    std::thread producer([&] {
        for(unsigned n=0;n<count;++n) {
            const std::array<float,2> frame={static_cast<float>(n),-static_cast<float>(n)};
            while(ring.write(frame)!=1) std::this_thread::yield();
        }
    });
    for(unsigned n=0;n<count;++n) {
        std::array<float,2> frame{};
        while(ring.peek(frame)!=1) std::this_thread::yield();
        if(frame[0]!=static_cast<float>(n) || frame[1]!=-static_cast<float>(n)) failed=true;
        ring.consume(1);
    }
    producer.join(); require(!failed,"SPSC ordering under concurrent load");
}
static void gain_ramp() {
    GainRamp ramp; ramp.target(1,100); std::vector<float> data(200,1);
    ramp.apply(data);
    near(data[0],0.01,1e-6,"first ramp frame"); near(data[198],1,1e-6,"last ramp frame");
    for(std::size_t n=0;n<100;++n) near(data[n*2],data[n*2+1],0,"channel gain mismatch");
    ramp.target(0,100); std::fill(data.begin(),data.end(),1.0f); ramp.apply(data); near(data.back(),0,1e-6,"mute ramp");
}
static void clock_estimator() {
    ClockRate rate; require(!rate.observe(0,10000000,48000),"clock must warm up");
    require(rate.observe(480048,110000000,48000)==false,"stale interval must be rejected");
    rate.reset(); rate.observe(0,10000000,48000);
    for(unsigned n=1;n<=20;++n) rate.observe(static_cast<std::uint64_t>(n)*480048,10000000ULL+static_cast<std::uint64_t>(n)*100000000,480000);
    // Independent clean one-second samples at +100 ppm.
    rate.reset(); rate.observe(0,10000000,480000);
    for(unsigned n=1;n<=20;++n) rate.observe(static_cast<std::uint64_t>(n)*480048,10000000ULL+static_cast<std::uint64_t>(n)*10000000,480000);
    require(rate.valid(),"clock did not become valid"); near(rate.relative(),1.0001,1e-8,"clock ppm estimate");
    rate.observe(1,10000001,480000); require(!rate.valid(),"backwards timestamp must invalidate clock");
}
static void drift_direction_and_bounds() {
    DriftController high,low;
    double a=1,b=1;
    for(unsigned n=0;n<10000;++n) { a=high.update(7000,6000,0.01); b=low.update(5000,6000,0.01); }
    require(a<1 && b>1,"drift sign is inverted");
    require(a>=0.9997-1e-12 && b<=1.0003+1e-12,"drift clamp");
    near(high.update(std::numeric_limits<double>::quiet_NaN(),6000,0.01),a,0,"NaN must not corrupt controller");
}
static void drift_long_run() {
    for(double input_rate:{1.0001,0.9999}) {
        DriftController controller; double queued=6000,ratio=1;
        for(unsigned n=0;n<360000;++n) {
            ratio=controller.update(queued,6000,0.01);
            queued+=480*input_rate-480/ratio;
            require(queued>1000 && queued<11000,"drift queue ran away");
        }
        near(queued,6000,30,"one-hour queue convergence");
    }
}
static void streaming_src() {
    Resampler resampler; std::vector<float> input(44100*2),output(50000*2);
    for(std::size_t f=0;f<44100;++f) {
        const auto value=static_cast<float>(0.25*std::sin(6.283185307179586*1000*static_cast<double>(f)/44100));
        input[f*2]=value; input[f*2+1]=-value;
    }
    std::size_t consumed=0,produced=0;
    while(consumed<44100) {
        const auto count=std::min<std::size_t>(441,44100-consumed);
        const auto done=resampler.process(std::span<const float>(input.data()+consumed*2,count*2),
            std::span<float>(output.data()+produced*2,output.size()-produced*2),48000.0/44100);
        require(done.consumed || done.produced,"SRC made no progress"); consumed+=done.consumed; produced+=done.produced;
    }
    require(produced>47800 && produced<=48000,"streaming sample count");
    for(std::size_t f=100;f<produced;++f) near(output[f*2],-output[f*2+1],1e-6,"SRC channel independence");
}
static void fanout_independent() {
    FanoutBus bus; bus.slot(0).enabled=true; bus.slot(1).enabled=true;
    std::vector<float> packet(480*2,0.25f);
    for(unsigned n=0;n<250;++n) { bus.publish(packet); bus.slot(0).queue.consume(480); }
    require(bus.slot(0).dropped_frames==0,"slow branch blocked healthy branch");
    require(bus.slot(1).dropped_frames>0 && bus.slot(1).resync,"overflow must be observable");
}
static void branch_priming_and_recovery() {
    BranchSlot slot; slot.enabled=true;
    BranchProcessor branch(slot,40); std::vector<float> output(480*2,99),packet(480*2,0.25f);
    branch.render(output); require(!branch.primed() && slot.underruns==0,"startup silence is not an underrun");
    for(unsigned n=0;n<10;++n) slot.queue.write(packet);
    for(unsigned n=0;n<20;++n) { slot.queue.write(packet); branch.render(output); }
    require(branch.primed(),"branch did not prime"); near(output.back(),0.25,0.002,"steady output level");
    require(slot.queue.available()>1500,"SRC must not hide the configured queue inside an oversized internal reservoir");
    for(unsigned n=0;n<30;++n) branch.render(output);
    require(slot.underruns>0 && !branch.primed(),"underflow must re-prime and count");
    slot.resync=true; branch.render(output); require(slot.recoveries==1,"explicit discontinuity must count");
}
static void mixer_bounds() {
    std::array<float,4> output{}; const std::array<float,4> input={0.8f,-0.8f,0.5f,-0.5f};
    mix_add(input,output,1); mix_add(input,output,1); limit_samples(output);
    near(output[0],1,0,"positive clipping protection"); near(output[1],-1,0,"negative clipping protection");
    output[0]=std::numeric_limits<float>::infinity(); output[1]=std::numeric_limits<float>::quiet_NaN();
    limit_samples(output); require(output[0]==0 && output[1]==0,"non-finite sanitization");
}
static void invalid_pcm_recovers() {
    BranchSlot slot;BranchProcessor processor(slot,40);
    std::vector<float> packet(960,0.25f),output(960);
    for(unsigned i=0;i<40;++i){slot.queue.write(packet);processor.render(output);}
    packet[0]=std::numeric_limits<float>::quiet_NaN();packet[1]=std::numeric_limits<float>::infinity();
    slot.queue.write(packet);processor.render(output);packet[0]=packet[1]=0.25f;
    for(unsigned i=0;i<40;++i) {
        slot.queue.write(packet);processor.render(output);
        for(auto sample:output)require(std::isfinite(sample),"invalid PCM escaped filter");
    }
    near(output.back(),0.25,0.002,"single invalid sample poisoned future audio");
    slot.resync=true;processor.render(output);require(slot.peak==0,"priming displayed stale peak");
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"ring wrap/full/partial",ring_wrap},{"SPSC concurrency",ring_concurrent},{"gain ramps",gain_ramp},
        {"clock validity",clock_estimator},{"drift direction/bounds",drift_direction_and_bounds},
        {"one-hour drift simulation",drift_long_run},{"streaming resampler",streaming_src},
        {"independent fanout",fanout_independent},{"priming/recovery",branch_priming_and_recovery},{"mixer bounds",mixer_bounds},
        {"invalid PCM recovery",invalid_pcm_recovers}};
    unsigned failed=0;
    for(const auto& [name,run]:tests) {
        try { run(); std::cout<<"PASS "<<name<<"\n"; }
        catch(const std::exception& error) { ++failed; std::cerr<<"FAIL "<<name<<": "<<error.what()<<"\n"; }
    }
    return failed ? 1 : 0;
}
