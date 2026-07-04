#pragma once

#include <cstdint>

class OamDma {
public:
    void reset();

    void start(uint8_t source_high_byte);

    void tick_dots(int dots);

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

    uint8_t dma_reg = 0; // stored value of OAM DMA register at FF46 address
    uint16_t source_base = 0; // start address for DMA copy

    int index = 0;             // 0..159
    int dot_counter = 0;       // copies every 4 dots
    int pending_copies = 0; // How many bytes to copy that are ready but the bus has not yet performed
};
