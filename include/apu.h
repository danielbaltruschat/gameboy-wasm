#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "dmg_clock.h"

struct Sample {
    float left = 0.0f;
    float right = 0.0f;
};

class APU {
public:
    static constexpr uint32_t cpu_clock_hz = 4'194'304;
    static constexpr uint32_t default_sample_rate_hz = 48'000;

    APU() = default;

    void reset();
    void tick_dots(dmg::DotCount dots);
    void clock_div_apu();

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    void set_sample_rate(uint32_t sample_rate_hz);
    std::span<const Sample> samples() const;
    void clear_samples();

private:
    struct LengthCounter {
        uint16_t value = 0;
        uint16_t max = 64;
        bool enabled = false;
    };

    struct Envelope {
        uint8_t initial_volume = 0;
        uint8_t volume = 0;
        uint8_t pace = 0;
        uint8_t timer = 0;
        bool increase = false;

        bool dac_enabled() const;
    };

    struct PulseChannel {
        bool enabled = false;

        uint8_t sweep = 0;       // NR10, channel 1 only
        uint8_t duty = 0;
        uint8_t duty_step = 0;

        LengthCounter length;
        Envelope envelope;

        uint16_t period = 0;
        uint16_t period_timer = 0;

        uint16_t sweep_shadow = 0;
        uint8_t sweep_timer = 0;
        bool sweep_enabled = false;
        bool sweep_negated = false;
    };

    struct WaveChannel {
        bool enabled = false;
        bool dac_enabled = false;

        LengthCounter length{0, 256, false};

        uint8_t output_level = 0;
        uint16_t period = 0;
        uint16_t period_timer = 0;
        uint8_t sample_index = 0;
        uint8_t sample_buffer = 0;
    };

    struct NoiseChannel {
        bool enabled = false;

        LengthCounter length;
        Envelope envelope;

        uint8_t polynomial = 0;
        uint16_t lfsr = 0;
        uint16_t period_timer = 0;
    };

    uint8_t nr50 = 0;
    uint8_t nr51 = 0;
    bool master_enabled = false;
    std::array<uint8_t, 0x30> registers{};

    PulseChannel ch1;
    PulseChannel ch2;
    WaveChannel ch3;
    NoiseChannel ch4;

    std::array<uint8_t, 16> wave_ram{};
    std::vector<Sample> sample_buffer;

    uint32_t sample_rate_hz = default_sample_rate_hz;
    int frame_sequencer_step = 0;
    float high_pass_capacitor_left = 0.0f;
    float high_pass_capacitor_right = 0.0f;
    double sample_cycles = 0.0;

    void tick_length_counters();
    void tick_envelopes();
    void tick_sweep();

    void tick_channels(dmg::DotCount dots);
    void tick_pulse(PulseChannel& channel, dmg::DotCount dots);
    void tick_wave(dmg::DotCount dots);
    void tick_noise(dmg::DotCount dots);

    void trigger_ch1();
    void trigger_ch2();
    void trigger_ch3();
    void trigger_ch4();

    uint8_t channel_status() const;
    uint8_t read_wave_ram(uint16_t addr) const;
    void write_wave_ram(uint16_t addr, uint8_t value);

    Sample mix() const;
    Sample apply_high_pass_filter(Sample sample);
    void push_samples_if_due(dmg::DotCount dots);
};
