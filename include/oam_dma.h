#pragma once

#include <cstdint>

#include "dmg_clock.h"


class OamDma {
public:
    void reset();

    void start(uint8_t source_high_byte);

    void tick_dots(dmg::DotCount dots);

    bool is_active() const;
    bool blocks_cpu_access(uint16_t addr) const;

    uint8_t read_reg() const;
    bool copy_pending() const;
    int pending_copy_count() const;
    uint16_t source_addr() const;
    uint16_t oam_offset() const;
    void acknowledge_copy();

private:
    bool active = false;

    uint8_t dma_reg = 0;
    uint16_t source_base = 0;

    int index = 0;             // 0..159
    int dot_counter = 0;       // copies every 4 dots
    int pending_copies = 0;
};
