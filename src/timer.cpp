#include "timer.h"

#include "interrupt_controller.h"

namespace {

bool bit_is_set(uint16_t value, int bit)
{
    return (value & (uint16_t{1} << bit)) != 0;
}

}

Timer::Timer(InterruptController& interrupts) : interrupts(interrupts)
{
    reset();
}

void Timer::reset() {
    div_counter = 0;
    tima = 0;
    tma = 0;
    tac = 0;
    previous_timer_input = timer_input();
    overflow_delay = 0;
    reload_cycle = false;
    div_apu_ticks = 0;
}

void Timer::tick_dots(int dots)
{
    if (dots <= 0) {
        return;
    }

    for (int i = 0; i < dots; i++) {
        reload_cycle = false;

        if (overflow_delay > 0) {
            overflow_delay--;

            if (overflow_delay == 0) {
                tima = tma;
                interrupts.request(Interrupt::Timer);
                reload_cycle = true;
            }
        }

        const bool previous_div_apu_input = bit_is_set(div_counter, div_apu_internal_bit);

        ++div_counter;

        if (previous_div_apu_input && !bit_is_set(div_counter, div_apu_internal_bit)) {
            ++div_apu_ticks;
        }

        update_timer_edge();
    }
}

uint8_t Timer::read(uint16_t addr) const
{
    switch (addr) {
    case 0xFF04: // DIV
        return static_cast<uint8_t>(div_counter >> 8); // read upper byte
    case 0xFF05: // TIMA
        return tima;
    case 0xFF06: // TMA
        return tma;
    case 0xFF07: // TAC
        return tac | 0xF8;
    default:
        return 0xFF;
    }
}

void Timer::write(uint16_t addr, uint8_t value)
{
    switch (addr) {
    case 0xFF04: {
        const bool previous_div_apu_input = bit_is_set(div_counter, div_apu_internal_bit);

        div_counter = 0;

        if (previous_div_apu_input) {
            ++div_apu_ticks;
        }

        update_timer_edge();
        break;
    }
    case 0xFF05:
        if (reload_cycle) {
            break;
        }
        tima = value;
        overflow_delay = 0;
        break;
    case 0xFF06:
        tma = value;
        if (reload_cycle) {
            tima = value;
        }
        break;
    case 0xFF07:
        tac = value & 0x07;
        update_timer_edge();
        break;
    default:
        break;
    }
}

int Timer::take_div_apu_ticks()
{
    const int ticks = div_apu_ticks;
    div_apu_ticks = 0;
    return ticks;
}

bool Timer::timer_enabled() const
{
    return (tac & 0x04) != 0;
}

int Timer::selected_timer_bit() const
{
    switch (tac & 0x03) {
    case 0x00:
        return timer_bit_4096_hz;
    case 0x01:
        return timer_bit_262144_hz;
    case 0x02:
        return timer_bit_65536_hz;
    case 0x03:
        return timer_bit_16384_hz;
    default:
        return timer_bit_4096_hz;
    }
}

bool Timer::timer_input() const
{
    return timer_enabled() && bit_is_set(div_counter, selected_timer_bit());
}

void Timer::update_timer_edge()
{
    const bool current_timer_input = timer_input();

    if (previous_timer_input && !current_timer_input) { // negative edge
        increment_tima();
    }

    previous_timer_input = current_timer_input;
}

void Timer::increment_tima()
{
    if (tima == 0xFF) {
        tima = 0x00;
        overflow_delay = 4;
        return;
    }

    ++tima;
}
