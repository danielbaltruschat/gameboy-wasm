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

void GameBoy::set_rtc_clock(Cartridge::RtcClock clock, void* context)
{
    cartridge.set_rtc_clock(clock, context);
}

void GameBoy::advance_rtc_milliseconds(uint64_t milliseconds)
{
    cartridge.advance_rtc_milliseconds(milliseconds);
}

bool GameBoy::has_battery() const
{
    return cartridge.has_battery();
}

bool GameBoy::has_rtc() const
{
    return cartridge.has_rtc();
}

bool GameBoy::battery_dirty() const
{
    return cartridge.battery_dirty();
}

uint64_t GameBoy::battery_revision() const
{
    return cartridge.battery_revision();
}

std::vector<uint8_t> GameBoy::battery_ram() const
{
    return cartridge.battery_ram();
}

std::vector<uint8_t> GameBoy::take_battery_ram()
{
    return cartridge.take_battery_ram();
}

bool GameBoy::load_battery_ram(std::span<const uint8_t> data)
{
    return cartridge.load_battery_ram(data);
}

Mbc3RtcRegisters GameBoy::rtc_registers()
{
    return cartridge.rtc_registers();
}

bool GameBoy::load_rtc_registers(const Mbc3RtcRegisters& registers)
{
    return cartridge.load_rtc_registers(registers);
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
