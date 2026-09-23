#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "boot_rom.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"


class GameBoy {
public:
    GameBoy();

    void load_boot_rom(std::span<const uint8_t> rom);
    void load_rom(std::span<const uint8_t> rom);
    void reset();

    void set_rtc_clock(Cartridge::RtcClock clock, void* context);
    void advance_rtc_milliseconds(uint64_t milliseconds);
    bool has_battery() const;
    bool has_rtc() const;
    bool battery_dirty() const;
    uint64_t battery_revision() const;
    std::vector<uint8_t> battery_ram() const;
    std::vector<uint8_t> take_battery_ram();
    bool load_battery_ram(std::span<const uint8_t> data);
    Mbc3RtcRegisters rtc_registers();
    bool load_rtc_registers(const Mbc3RtcRegisters& registers);

    bool step_m_cycle();
    int step_instruction();          // execute one CPU instruction, tick hardware
    void step_frame();               // run until one video frame is produced

    void set_button(JoypadButton button, bool pressed);

    const Framebuffer& framebuffer() const;

    bool frame_ready() const;
    void clear_frame_ready();

private:
    // Independent hardware/state
    InterruptController interrupts;
    BootRom boot_rom;
    Memory memory;
    Cartridge cartridge;

    // Hardware with direct interrupt access
    PPU ppu;

    // Components that depend on shared hardware
    Timer timer;
    Joypad joypad;
    MemBus bus;
    CPU cpu;

    uint64_t total_dots = 0;

    static void tick_cpu_dot(void* context);
    void tick_dot();
};
