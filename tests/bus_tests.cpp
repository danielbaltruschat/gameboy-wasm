#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "apu.h"
#include "boot_rom.h"
#include "bus.h"
#include "cartridge.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "serial.h"
#include "timer.h"

Serial::Serial(InterruptController& interrupts) : interrupts(interrupts) {}

namespace {

std::vector<uint8_t> make_rom()
{
    std::vector<uint8_t> rom(0x8000, 0x00);
    rom[0x0147] = 0x00;
    rom[0x0149] = 0x00;
    return rom;
}

struct BusFixture {
    InterruptController interrupts;
    BootRom boot_rom;
    Memory memory;
    std::vector<uint8_t> rom = make_rom();
    Cartridge cartridge{rom};
    PPU ppu{interrupts};
    APU apu;
    Timer timer{interrupts};
    Joypad joypad{interrupts};
    Serial serial{interrupts};
    MemBus bus{
        boot_rom,
        cartridge,
        ppu,
        apu,
        timer,
        joypad,
        serial,
        interrupts,
        memory,
    };

    BusFixture()
    {
        interrupts.reset();
        memory.reset();
        ppu.reset();
        timer.reset();
        joypad.reset();
    }
};

} // namespace

TEST_CASE("DMG OAM DMA mirrors sources E000 through FFFF into work RAM")
{
    BusFixture fixture;

    fixture.memory.write_wram(0xC000, 0x12);
    fixture.memory.write_wram(0xDE00, 0x34);
    fixture.memory.write_wram(0xDF00, 0x56);

    REQUIRE(fixture.bus.read_dma_source(0xE000) == 0x12);
    REQUIRE(fixture.bus.read_dma_source(0xFE00) == 0x34);
    REQUIRE(fixture.bus.read_dma_source(0xFF00) == 0x56);
}

TEST_CASE("OAM DMA reads VRAM through the PPU access lock")
{
    BusFixture fixture;

    fixture.ppu.write(0x8000, 0xA5);
    fixture.ppu.write(0xFF40, 0x80);
    fixture.ppu.tick_dots(79);

    REQUIRE(fixture.ppu.read(0x8000) == 0xFF);
    REQUIRE(fixture.bus.read_dma_source(0x8000) == 0xA5);
}

TEST_CASE("External OAM DMA redirects CPU conflicts but leaves VRAM and IO available")
{
    BusFixture fixture;

    fixture.memory.write_wram(0xC000, 0x22);
    fixture.memory.write_wram(0xC123, 0x44);
    fixture.bus.write(0x8000, 0x11);
    fixture.bus.write(0xFF46, 0xC0);
    fixture.bus.tick_dma_dots(4);

    REQUIRE(fixture.bus.read(0xC123) == 0x44);

    fixture.bus.tick_dma_dots(4);

    REQUIRE(fixture.bus.read(0xC123) == 0x22);
    REQUIRE(fixture.bus.read(0x8000) == 0x11);

    fixture.bus.write(0x8000, 0x33);
    fixture.bus.write(0xFF0F, 0x04);
    REQUIRE(fixture.bus.read(0x8000) == 0x33);
    REQUIRE((fixture.bus.read(0xFF0F) & 0x04) == 0x04);

    fixture.bus.write(0xC123, 0x0F);
    REQUIRE(fixture.ppu.read(0xFE00) == 0x02);
    REQUIRE(fixture.bus.read(0xFE00) == 0xFF);
}

TEST_CASE("VRAM OAM DMA leaves the external bus available")
{
    BusFixture fixture;

    fixture.ppu.write(0x8000, 0xA5);
    fixture.memory.write_wram(0xC000, 0x44);
    fixture.bus.write(0xFF46, 0x80);
    fixture.bus.tick_dma_dots(8);

    REQUIRE(fixture.bus.read(0x8001) == 0xA5);
    REQUIRE(fixture.bus.read(0xC000) == 0x44);

    fixture.bus.write(0xC000, 0x66);
    REQUIRE(fixture.bus.read(0xC000) == 0x66);
}

TEST_CASE("Unmapped cartridge RAM reads the external bus latch")
{
    BusFixture fixture;

    fixture.bus.write(0xC000, 0x5A);

    REQUIRE(fixture.bus.read(0xA000) == 0x5A);
}
