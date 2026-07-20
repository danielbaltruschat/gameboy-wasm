#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cartridge.h"

namespace {

constexpr std::size_t rom_bank_size = 0x4000;

std::vector<uint8_t> make_rom(
    std::size_t bank_count,
    uint8_t cartridge_type,
    uint8_t rom_size_code,
    uint8_t ram_size_code = 0x00
)
{
    std::vector<uint8_t> rom(bank_count * rom_bank_size, 0x00);

    for (std::size_t bank = 0; bank < bank_count; ++bank) {
        const std::size_t start = bank * rom_bank_size;
        std::fill_n(rom.begin() + start, rom_bank_size, static_cast<uint8_t>(bank));
        rom[start] = static_cast<uint8_t>(bank & 0xFF);
        rom[start + 1] = static_cast<uint8_t>((bank >> 8) & 0x01);
    }

    rom[0x0147] = cartridge_type;
    rom[0x0148] = rom_size_code;
    rom[0x0149] = ram_size_code;

    return rom;
}

} // namespace

TEST_CASE("Cartridge reports no ROM before loading")
{
    Cartridge cartridge;

    REQUIRE_FALSE(cartridge.loaded());
}

TEST_CASE("Cartridge loads and owns a copy of ROM data")
{
    auto rom = make_rom(2, 0x00, 0x00);
    const uint8_t original = rom[0x0200];
    Cartridge cartridge(rom);

    rom[0x0200] = static_cast<uint8_t>(original + 1);

    REQUIRE(cartridge.loaded());
    REQUIRE(cartridge.read(0x0200) == original);
}

TEST_CASE("Cartridge loading a replacement ROM resets bank selection")
{
    Cartridge cartridge;
    auto first = make_rom(4, 0x01, 0x01);
    auto second = make_rom(4, 0x01, 0x01);
    second[0x4000] = 0xA5;

    cartridge.load_rom(first);
    cartridge.write(0x2000, 0x02);
    REQUIRE(cartridge.read(0x4000) == 0x02);

    cartridge.load_rom(second);

    REQUIRE(cartridge.read(0x4000) == 0xA5);
}

TEST_CASE("Cartridge parses the hardware configuration from the ROM header")
{
    const auto rom = make_rom(2, 0x03, 0x00, 0x03);
    Cartridge cartridge(rom);
    const CartridgeHeader& header = cartridge.get_header();

    REQUIRE(header.cartridgeType == 0x03);
    REQUIRE(header.ramSizeBytes == 32 * 1024);
}

TEST_CASE("Cartridge decodes standard external RAM size codes")
{
    struct RamSizeCase {
        uint8_t code;
        std::size_t bytes;
    };

    constexpr std::array cases{
        RamSizeCase{0x00, 0},
        RamSizeCase{0x01, 0},
        RamSizeCase{0x02, 8 * 1024},
        RamSizeCase{0x03, 32 * 1024},
        RamSizeCase{0x04, 128 * 1024},
        RamSizeCase{0x05, 64 * 1024},
        RamSizeCase{0xFF, 0},
    };

    for (const auto& test_case : cases) {
        Cartridge cartridge(make_rom(2, 0x1A, 0x00, test_case.code));
        const CartridgeHeader& header = cartridge.get_header();

        REQUIRE(header.ramSizeBytes == test_case.bytes);
    }
}

TEST_CASE("Cartridge type selects mapper capabilities")
{
    struct CapabilityCase {
        uint8_t type;
        uint8_t ram_size_code;
        MapperType mapper;
        bool has_ram;
        bool has_battery;
        bool has_timer;
        bool has_rumble;
    };

    constexpr std::array cases{
        CapabilityCase{0x00, 0x00, MapperType::RomOnly, false, false, false, false},
        CapabilityCase{0x09, 0x02, MapperType::RomOnly, true, true, false, false},
        CapabilityCase{0x01, 0x00, MapperType::Mbc1, false, false, false, false},
        CapabilityCase{0x03, 0x03, MapperType::Mbc1, true, true, false, false},
        CapabilityCase{0x05, 0x00, MapperType::Mbc2, true, false, false, false},
        CapabilityCase{0x06, 0x00, MapperType::Mbc2, true, true, false, false},
        CapabilityCase{0x0F, 0x00, MapperType::Mbc3, false, true, true, false},
        CapabilityCase{0x10, 0x03, MapperType::Mbc3, true, true, true, false},
        CapabilityCase{0x11, 0x00, MapperType::Mbc3, false, false, false, false},
        CapabilityCase{0x13, 0x03, MapperType::Mbc3, true, true, false, false},
        CapabilityCase{0x19, 0x00, MapperType::Mbc5, false, false, false, false},
        CapabilityCase{0x1B, 0x04, MapperType::Mbc5, true, true, false, false},
        CapabilityCase{0x1E, 0x04, MapperType::Mbc5, true, true, false, true},
        CapabilityCase{0xFC, 0x00, MapperType::Unknown, false, false, false, false},
    };

    for (const auto& test_case : cases) {
        Cartridge cartridge(make_rom(2, test_case.type, 0x00, test_case.ram_size_code));
        const CartridgeCapabilities& capabilities = cartridge.get_capabilities();

        REQUIRE(capabilities.mapper == test_case.mapper);
        REQUIRE(capabilities.hasRam == test_case.has_ram);
        REQUIRE(capabilities.hasBattery == test_case.has_battery);
        REQUIRE(capabilities.hasTimer == test_case.has_timer);
        REQUIRE(capabilities.hasRumble == test_case.has_rumble);
        REQUIRE_FALSE(capabilities.hasMbc1MulticartLayout);
    }
}

TEST_CASE("ROM-only cartridge maps both ROM banks and ignores writes")
{
    Cartridge cartridge(make_rom(2, 0x00, 0x00));

    REQUIRE(cartridge.read(0x0000) == 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x01);
    REQUIRE(cartridge.read(0x7FFF) == 0x01);

    cartridge.write(0x2000, 0x7F);

    REQUIRE(cartridge.read(0x4000) == 0x01);
}

TEST_CASE("ROM-only cartridge exposes RAM directly when present")
{
    Cartridge cartridge(make_rom(2, 0x08, 0x00, 0x02));

    cartridge.write(0xA000, 0x12);
    cartridge.write(0xBFFF, 0x34);

    REQUIRE(cartridge.read(0xA000) == 0x12);
    REQUIRE(cartridge.read(0xBFFF) == 0x34);
}

TEST_CASE("ROM-only cartridge without RAM returns open bus")
{
    Cartridge cartridge(make_rom(2, 0x00, 0x00));

    cartridge.write(0xA000, 0x12);

    REQUIRE(cartridge.read(0xA000) == 0xFF);
}

TEST_CASE("MBC1 selects lower and upper ROM bank bits")
{
    Cartridge cartridge(make_rom(128, 0x01, 0x06));

    REQUIRE(cartridge.read(0x4000) == 0x01);

    cartridge.write(0x2000, 0x02);
    REQUIRE(cartridge.read(0x4000) == 0x02);

    cartridge.write(0x4000, 0x01);
    REQUIRE(cartridge.read(0x4000) == 0x22);

    cartridge.write(0x2000, 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x21);
}

TEST_CASE("MBC1 advanced mode banks the lower ROM window")
{
    Cartridge cartridge(make_rom(128, 0x01, 0x06));

    cartridge.write(0x4000, 0x02);
    REQUIRE(cartridge.read(0x0000) == 0x00);

    cartridge.write(0x6000, 0x01);

    REQUIRE(cartridge.read(0x0000) == 0x40);
    REQUIRE(cartridge.read(0x4000) == 0x41);
}

TEST_CASE("MBC1 enables and banks external RAM")
{
    Cartridge cartridge(make_rom(2, 0x03, 0x00, 0x03));

    REQUIRE(cartridge.read(0xA000) == 0xFF);
    cartridge.write(0xA000, 0x99);

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x6000, 0x01);

    cartridge.write(0x4000, 0x00);
    cartridge.write(0xA000, 0x10);
    cartridge.write(0x4000, 0x01);
    cartridge.write(0xA000, 0x11);
    cartridge.write(0x4000, 0x02);
    cartridge.write(0xA000, 0x12);

    cartridge.write(0x4000, 0x00);
    REQUIRE(cartridge.read(0xA000) == 0x10);
    cartridge.write(0x4000, 0x01);
    REQUIRE(cartridge.read(0xA000) == 0x11);
    cartridge.write(0x4000, 0x02);
    REQUIRE(cartridge.read(0xA000) == 0x12);

    cartridge.write(0x0000, 0x00);
    REQUIRE(cartridge.read(0xA000) == 0xFF);
}

TEST_CASE("MBC1 reset restores mapper registers without erasing RAM")
{
    Cartridge cartridge(make_rom(4, 0x03, 0x01, 0x02));

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x2000, 0x02);
    cartridge.write(0xA000, 0x5A);
    REQUIRE(cartridge.read(0x4000) == 0x02);

    cartridge.reset_mapper();

    REQUIRE(cartridge.read(0x4000) == 0x01);
    REQUIRE(cartridge.read(0xA000) == 0xFF);
    cartridge.write(0x0000, 0x0A);
    REQUIRE(cartridge.read(0xA000) == 0x5A);
}

TEST_CASE("MBC2 uses address bit eight to select its control register")
{
    Cartridge cartridge(make_rom(16, 0x05, 0x03));

    cartridge.write(0x2100, 0x03);
    REQUIRE(cartridge.read(0x4000) == 0x03);

    cartridge.write(0x2000, 0x04);
    REQUIRE(cartridge.read(0x4000) == 0x03);

    cartridge.write(0x2100, 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x01);
}

TEST_CASE("MBC2 stores nibble RAM and mirrors its address range")
{
    Cartridge cartridge(make_rom(16, 0x05, 0x03));

    REQUIRE(cartridge.read(0xA123) == 0xFF);
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0xA123, 0xAB);

    REQUIRE(cartridge.read(0xA123) == 0xFB);
    REQUIRE(cartridge.read(0xA323) == 0xFB);

    cartridge.write(0x0000, 0x00);
    REQUIRE(cartridge.read(0xA123) == 0xFF);
}

TEST_CASE("MBC3 switches ROM and external RAM banks")
{
    Cartridge cartridge(make_rom(8, 0x13, 0x02, 0x03));

    cartridge.write(0x2000, 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x01);
    cartridge.write(0x2000, 0x05);
    REQUIRE(cartridge.read(0x4000) == 0x05);

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x00);
    cartridge.write(0xA000, 0x20);
    cartridge.write(0x4000, 0x02);
    cartridge.write(0xA000, 0x22);

    cartridge.write(0x4000, 0x00);
    REQUIRE(cartridge.read(0xA000) == 0x20);
    cartridge.write(0x4000, 0x02);
    REQUIRE(cartridge.read(0xA000) == 0x22);
}

TEST_CASE("MBC3 latches a stable RTC snapshot")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 10);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);

    REQUIRE(cartridge.read(0xA000) == 10);

    cartridge.tick_rtc_seconds(5);
    REQUIRE(cartridge.read(0xA000) == 10);

    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    REQUIRE(cartridge.read(0xA000) == 15);
}

TEST_CASE("MBC3 RTC rolls over its day counter and sets carry")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);

    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 58);
    cartridge.write(0x4000, 0x09);
    cartridge.write(0xA000, 59);
    cartridge.write(0x4000, 0x0A);
    cartridge.write(0xA000, 23);
    cartridge.write(0x4000, 0x0B);
    cartridge.write(0xA000, 0xFF);
    cartridge.write(0x4000, 0x0C);
    cartridge.write(0xA000, 0x01);

    cartridge.tick_rtc_seconds(2);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);

    cartridge.write(0x4000, 0x08);
    REQUIRE(cartridge.read(0xA000) == 0);
    cartridge.write(0x4000, 0x09);
    REQUIRE(cartridge.read(0xA000) == 0);
    cartridge.write(0x4000, 0x0A);
    REQUIRE(cartridge.read(0xA000) == 0);
    cartridge.write(0x4000, 0x0B);
    REQUIRE(cartridge.read(0xA000) == 0);
    cartridge.write(0x4000, 0x0C);
    REQUIRE((cartridge.read(0xA000) & 0x81) == 0x80);
}

TEST_CASE("MBC3 RTC does not advance while halted")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 20);
    cartridge.write(0x4000, 0x0C);
    cartridge.write(0xA000, 0x40);

    cartridge.tick_rtc_seconds(30);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    cartridge.write(0x4000, 0x08);

    REQUIRE(cartridge.read(0xA000) == 20);
}

TEST_CASE("MBC3 reset preserves the live battery-backed RTC")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 12);

    cartridge.reset_mapper();
    REQUIRE(cartridge.read(0xA000) == 0xFF);

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    cartridge.write(0x4000, 0x08);

    REQUIRE(cartridge.read(0xA000) == 12);
}

TEST_CASE("MBC5 selects all nine ROM bank bits and permits bank zero")
{
    Cartridge cartridge(make_rom(512, 0x19, 0x08));

    cartridge.write(0x2000, 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x00);
    REQUIRE(cartridge.read(0x4001) == 0x00);

    cartridge.write(0x2000, 0x01);
    cartridge.write(0x3000, 0x01);

    REQUIRE(cartridge.read(0x4000) == 0x01);
    REQUIRE(cartridge.read(0x4001) == 0x01);
}

TEST_CASE("MBC5 selects sixteen RAM banks without rumble")
{
    Cartridge cartridge(make_rom(2, 0x1B, 0x00, 0x04));
    cartridge.write(0x0000, 0x0A);

    cartridge.write(0x4000, 0x00);
    cartridge.write(0xA000, 0x30);
    cartridge.write(0x4000, 0x0F);
    cartridge.write(0xA000, 0x3F);

    cartridge.write(0x4000, 0x00);
    REQUIRE(cartridge.read(0xA000) == 0x30);
    cartridge.write(0x4000, 0x0F);
    REQUIRE(cartridge.read(0xA000) == 0x3F);
}

TEST_CASE("MBC5 rumble bit is not part of the RAM bank number")
{
    Cartridge cartridge(make_rom(2, 0x1E, 0x00, 0x04));
    cartridge.write(0x0000, 0x0A);

    cartridge.write(0x4000, 0x00);
    cartridge.write(0xA000, 0x45);
    cartridge.write(0x4000, 0x08);

    REQUIRE(cartridge.read(0xA000) == 0x45);
}
