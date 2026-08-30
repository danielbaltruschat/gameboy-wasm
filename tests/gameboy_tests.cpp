#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "gameboy.h"

namespace {

std::vector<uint8_t> make_rom()
{
    std::vector<uint8_t> rom(0x8000, 0x00);
    rom[0x0147] = 0x00;
    rom[0x0149] = 0x00;
    return rom;
}

} // namespace

TEST_CASE("GameBoy advances every M-cycle of one CPU instruction")
{
    auto rom = make_rom();
    rom[0x0000] = 0x01; // LD BC,d16
    rom[0x0001] = 0x34;
    rom[0x0002] = 0x12;
    rom[0x0003] = 0x00; // NOP

    GameBoy gameboy;
    gameboy.load_rom(rom);
    gameboy.reset();

    REQUIRE(gameboy.step_instruction() == 3);
    REQUIRE(gameboy.step_instruction() == 1);
}

TEST_CASE("GameBoy executes the mapped boot ROM before the cartridge")
{
    auto rom = make_rom();
    rom[0x0000] = 0x01; // LD BC,d16

    std::array<uint8_t, BootRom::dmg_size> boot_rom{};
    boot_rom[0x0000] = 0x00; // NOP

    GameBoy gameboy;
    gameboy.load_rom(rom);
    gameboy.load_boot_rom(boot_rom);
    gameboy.reset();

    REQUIRE(gameboy.step_instruction() == 1);
}

TEST_CASE("GameBoy runs coordinated M-cycles until the PPU produces a frame")
{
    auto rom = make_rom();
    rom[0x0000] = 0x21; // LD HL,FF40
    rom[0x0001] = 0x40;
    rom[0x0002] = 0xFF;
    rom[0x0003] = 0x36; // LD (HL),80: enable LCD
    rom[0x0004] = 0x80;

    GameBoy gameboy;
    gameboy.load_rom(rom);
    gameboy.reset();

    REQUIRE(gameboy.step_instruction() == 3);
    REQUIRE(gameboy.step_instruction() == 3);
    REQUIRE_FALSE(gameboy.frame_ready());

    gameboy.step_frame();

    REQUIRE(gameboy.frame_ready());
    gameboy.clear_frame_ready();
    REQUIRE_FALSE(gameboy.frame_ready());
}

TEST_CASE("GameBoy reset clears coordinated frame state")
{
    auto rom = make_rom();
    rom[0x0000] = 0x21; // LD HL,FF40
    rom[0x0001] = 0x40;
    rom[0x0002] = 0xFF;
    rom[0x0003] = 0x36; // LD (HL),80: enable LCD
    rom[0x0004] = 0x80;

    GameBoy gameboy;
    gameboy.load_rom(rom);
    gameboy.reset();
    gameboy.step_instruction();
    gameboy.step_instruction();
    gameboy.step_frame();
    REQUIRE(gameboy.frame_ready());

    gameboy.reset();

    REQUIRE_FALSE(gameboy.frame_ready());
}

TEST_CASE("GameBoy STOP wakes only from a selected falling joypad line")
{
    auto rom = make_rom();
    rom[0x0000] = 0x3E; // LD A,10: select action buttons only.
    rom[0x0001] = 0x10;
    rom[0x0002] = 0xE0; // LDH (00),A
    rom[0x0003] = 0x00;
    rom[0x0004] = 0x10; // STOP 00
    rom[0x0005] = 0x00;
    rom[0x0006] = 0x00; // NOP after wake-up.

    GameBoy gameboy;
    gameboy.load_rom(rom);
    gameboy.reset();

    REQUIRE(gameboy.step_instruction() == 2);
    REQUIRE(gameboy.step_instruction() == 3);
    REQUIRE(gameboy.step_instruction() == 1);
    REQUIRE_FALSE(gameboy.step_m_cycle());
    REQUIRE(gameboy.step_instruction() == 0);

    gameboy.set_button(JoypadButton::Right, true);
    REQUIRE_FALSE(gameboy.step_m_cycle());

    gameboy.set_button(JoypadButton::A, true);
    for (int cycle = 0; cycle < 32767; ++cycle) {
        REQUIRE_FALSE(gameboy.step_m_cycle());
    }
    REQUIRE(gameboy.step_m_cycle());
}
