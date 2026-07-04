#pragma once

#include <array>
#include <cstdint>

class Memory {
public:
    void reset();

    uint8_t read_wram(uint16_t addr) const;
    void write_wram(uint16_t addr, uint8_t value);

    uint8_t read_hram(uint16_t addr) const;
    void write_hram(uint16_t addr, uint8_t value);

private:
    std::array<uint8_t, 0x2000> wram; // 8 KiB: C000-DFFF
    std::array<uint8_t, 0x7F> hram;   // 127 bytes: FF80-FFFE
};
