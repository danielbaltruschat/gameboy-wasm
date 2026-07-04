#pragma once

#include <cstdint>

#include "dmg_clock.h"

class InterruptController;

class Serial {
public:
    Serial(InterruptController& interrupts);

    void reset();

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    void tick_dots(dmg::DotCount dots);
    void clock_external_bit(bool incoming_bit);

    bool transfer_active() const;
    bool using_internal_clock() const;
    bool outgoing_bit() const;

private:
    InterruptController& interrupts;

    uint8_t sb = 0; // FF01
    uint8_t sc = 0; // FF02
    int transfer_dots = 0;
    uint8_t bits_shifted = 0;

    void complete_transfer();
};
