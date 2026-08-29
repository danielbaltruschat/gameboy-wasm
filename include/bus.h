#pragma once

#include <cstdint>

#include "boot_rom.h"
#include "oam_dma.h"

class APU;
class Cartridge;
class InterruptController;
class Joypad;
class Memory;
class PPU;
class Serial;
class Timer;

enum class BusAccessType {
    Read,
    Write,
    Internal,
    ReadAndInternal,
};


class MemBus {
public:
    MemBus(
        BootRom& boot_rom,
        Cartridge& cartridge,
        PPU& ppu,
        APU& apu,
        Timer& timer,
        Joypad& joypad,
        Serial& serial,
        InterruptController& interrupts,
        Memory& memory
    )
        : boot_rom(boot_rom),
          cartridge(cartridge),
          ppu(ppu),
          apu(apu),
          timer(timer),
          joypad(joypad),
          serial(serial),
          interrupts(interrupts),
          memory(memory)
    {}

    void reset();

    uint8_t read(uint16_t addr);
    uint8_t read_and_internal(uint16_t addr);
    void write(uint16_t addr, uint8_t value);
    void internal_cycle(uint16_t addr);

    void tick_dma_dots(int dots);
    bool dma_active() const;

    uint8_t read_dma_source(uint16_t addr);
    void notify_cpu_address_bus(uint16_t addr, BusAccessType access_type);

private:
    BootRom& boot_rom;
    Cartridge& cartridge;
    PPU& ppu;
    APU& apu;
    Timer& timer;
    Joypad& joypad;
    Serial& serial;
    InterruptController& interrupts;
    Memory& memory;
    OamDma oam_dma;
    uint8_t open_bus_value = 0xFF;

    uint8_t read_unchecked(uint16_t addr);
    void write_unchecked(uint16_t addr, uint8_t value);
    void update_open_bus(uint16_t addr, uint8_t value);
    uint8_t read_blocked_by_dma(uint16_t addr);
    void write_blocked_by_dma(uint8_t value);
};
