#pragma once

#include <cstdint>
#include <span>

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
    uint32_t rtc_dots = 0;

    static void tick_cpu_dot(void* context);
    void tick_dot();
};
