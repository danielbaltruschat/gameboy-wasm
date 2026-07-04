#pragma once

#include <cstdint>
#include <span>

#include "apu.h"
#include "boot_rom.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "serial.h"
#include "timer.h"


class GameBoy {
public:
    GameBoy();

    void load_boot_rom(std::span<const uint8_t> rom);
    void load_rom(std::span<const uint8_t> rom);
    void reset();

    void step_m_cycle();
    int step_instruction();          // execute one CPU instruction, tick hardware
    void step_frame();               // run until one video frame is produced

    void set_button(JoypadButton button, bool pressed);

    const Framebuffer& framebuffer() const;
    std::span<const Sample> take_audio_samples();

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
    APU apu;

    // Components that depend on shared hardware
    Timer timer;
    Joypad joypad;
    Serial serial;       // optional initially
    MemBus bus;
    CPU cpu;

    // Emulator state
    bool running;
    bool paused;
    uint64_t total_dots;
};
