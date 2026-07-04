#pragma once

#include <cstdint>

#include "dmg_clock.h"

class InterruptController;

class Timer {
public:
    Timer(InterruptController& interrupts);

    void reset();

    void tick_dots(dmg::DotCount dots);

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    int take_div_apu_ticks();

private:
    static constexpr int timer_bit_4096_hz = 9;
    static constexpr int timer_bit_262144_hz = 3;
    static constexpr int timer_bit_65536_hz = 5;
    static constexpr int timer_bit_16384_hz = 7;
    static constexpr int div_apu_internal_bit = 12; // FF04 bit 4.

    InterruptController& interrupts;

    uint16_t div_counter; // Internal 16-bit system counter; FF04 exposes high byte.

    uint8_t tima; // FF05
    uint8_t tma;  // FF06
    uint8_t tac;  // FF07
    bool previous_timer_input = false;
    int overflow_delay = 0;
    int div_apu_ticks = 0;

    bool timer_enabled() const;
    int selected_timer_bit() const;
    bool timer_input() const;
    void update_timer_edge();
    void increment_tima();
};
