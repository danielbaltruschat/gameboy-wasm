#include "gameboy.h"

GameBoy::GameBoy()
    : ppu(interrupts),
      timer(interrupts),
      joypad(interrupts),
      bus(boot_rom, cartridge, ppu, timer, joypad, interrupts, memory),
      cpu(&bus, &interrupts, false)
{
    reset();
}

void GameBoy::load_boot_rom(std::span<const uint8_t> rom)
{
    boot_rom.load_dmg(rom);
}

void GameBoy::load_rom(std::span<const uint8_t> rom)
{
    cartridge.load_rom(rom);
}

void GameBoy::reset()
{
    interrupts.reset();
    boot_rom.reset();
    memory.reset();
    cartridge.reset_mapper();
    ppu.reset();
    timer.reset();
    joypad.reset();
    bus.reset();
    cpu.reset();
    total_dots = 0;
}

bool GameBoy::step_m_cycle()
{
    return cpu.step_m_cycle(tick_cpu_dot, this);
}

void GameBoy::tick_cpu_dot(void* context)
{
    static_cast<GameBoy*>(context)->tick_dot();
}

void GameBoy::tick_dot()
{
    timer.tick_dots(1);
    ppu.tick_dots(1);
    bus.tick_dma_dots(1);
    joypad.tick_dots(1);

    ++total_dots;
    cartridge.tick_rtc_dots(1);
}

int GameBoy::step_instruction()
{
    int m_cycles = 0;
    do {
        if (!step_m_cycle()) {
            return m_cycles;
        }
        ++m_cycles;
    } while (!cpu.instruction_boundary() && !cpu.is_locked_up());

    return m_cycles;
}

void GameBoy::step_frame()
{
    ppu.clear_frame_ready();
    do {
        if (!step_m_cycle()) {
            return;
        }
    } while (!ppu.is_frame_ready());
}

void GameBoy::set_button(JoypadButton button, bool pressed)
{
    const uint8_t previous_value = joypad.read();
    joypad.set_button(button, pressed);
    const uint8_t falling_lines = static_cast<uint8_t>(
        previous_value & ~joypad.read() & 0x0F
    );
    if (falling_lines != 0) {
        cpu.wake_from_stop();
    }
}

const Framebuffer& GameBoy::framebuffer() const
{
    return ppu.get_framebuffer();
}

bool GameBoy::frame_ready() const
{
    return ppu.is_frame_ready();
}

void GameBoy::clear_frame_ready()
{
    ppu.clear_frame_ready();
}
