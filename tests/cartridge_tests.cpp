#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cartridge.h"

namespace {

constexpr std::size_t rom_bank_size = 0x4000;

struct RtcClock {
    uint64_t milliseconds = 0;

    static uint64_t now(void* context)
    {
        return static_cast<RtcClock*>(context)->milliseconds;
    }
};

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

TEST_CASE("Battery RAM snapshots preserve standard mapper RAM and clear dirty state")
{
    Cartridge cartridge(make_rom(4, 0x03, 0x01, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x01);
    cartridge.write(0xA000, 0xA5);

    REQUIRE(cartridge.battery_dirty());
    REQUIRE(cartridge.battery_revision() != 0);
    const std::vector<uint8_t> snapshot = cartridge.take_battery_ram();
    REQUIRE_FALSE(cartridge.battery_dirty());

    Cartridge restored(make_rom(4, 0x03, 0x01, 0x03));
    REQUIRE(restored.load_battery_ram(snapshot));
    restored.write(0x0000, 0x0A);
    restored.write(0x4000, 0x01);
    REQUIRE(restored.read(0xA000) == 0xA5);
}

TEST_CASE("Battery RAM snapshots preserve MBC2 nibble RAM")
{
    Cartridge cartridge(make_rom(4, 0x06, 0x01));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0xA000, 0xAB);
    cartridge.write(0xA1FF, 0x04);

    const std::vector<uint8_t> snapshot = cartridge.take_battery_ram();
    REQUIRE(snapshot.size() == 512);
    REQUIRE(snapshot[0] == 0x0B);
    REQUIRE(snapshot[0x1FF] == 0x04);

    Cartridge restored(make_rom(4, 0x06, 0x01));
    REQUIRE(restored.load_battery_ram(snapshot));
    restored.write(0x0000, 0x0A);
    REQUIRE(restored.read(0xA000) == 0xFB);
    REQUIRE(restored.read(0xA1FF) == 0xF4);
}

TEST_CASE("Battery RAM rejects a payload with the wrong size without changing RAM")
{
    Cartridge cartridge(make_rom(4, 0x03, 0x01, 0x02));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0xA000, 0x31);

    REQUIRE_FALSE(cartridge.load_battery_ram({}));
    REQUIRE(cartridge.read(0xA000) == 0x31);
}

TEST_CASE("MBC3 RTC uses the injected clock and preserves its raw state")
{
    RtcClock clock;
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.set_rtc_callback(RtcClock::now, &clock);
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 10);

    clock.milliseconds = 2'000;
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    REQUIRE(cartridge.read(0xA000) == 12);

    const Mbc3RtcRegisters state = cartridge.rtc_registers();
    Cartridge restored(make_rom(8, 0x10, 0x02, 0x03));
    REQUIRE(restored.load_rtc_registers(state));
    const Mbc3RtcRegisters restored_state = restored.rtc_registers();
    REQUIRE(restored_state.seconds == 12);
    REQUIRE(restored_state.subsecond_ticks == state.subsecond_ticks);
    REQUIRE(restored_state.subsecond_tick_thousandths == state.subsecond_tick_thousandths);
}

TEST_CASE("MBC3 RTC does not advance its injected clock while halted")
{
    RtcClock clock;
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.set_rtc_callback(RtcClock::now, &clock);
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 20);
    cartridge.write(0x4000, 0x0C);
    cartridge.write(0xA000, 0x40);

    clock.milliseconds = 30'000;
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    cartridge.write(0x4000, 0x08);
    REQUIRE(cartridge.read(0xA000) == 20);
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

TEST_CASE("MBC1 detects and maps multicart sub-ROM wiring")
{
    auto rom = make_rom(64, 0x01, 0x05);
    constexpr std::size_t logo_offset = 0x0104;
    constexpr std::size_t logo_size = 48;
    constexpr std::size_t sub_rom_size = 0x40000;

    for (std::size_t sub_rom = 1; sub_rom < 4; ++sub_rom) {
        std::copy_n(
            rom.begin() + logo_offset,
            logo_size,
            rom.begin() + sub_rom * sub_rom_size + logo_offset
        );
    }

    Cartridge cartridge(rom);
    REQUIRE(cartridge.get_capabilities().hasMbc1MulticartLayout);

    cartridge.write(0x4000, 0x01);
    cartridge.write(0x2000, 0x00);
    REQUIRE(cartridge.read(0x4000) == 0x11);

    cartridge.write(0x2000, 0x10);
    REQUIRE(cartridge.read(0x4000) == 0x10);

    cartridge.write(0x6000, 0x01);
    REQUIRE(cartridge.read(0x0000) == 0x10);
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

TEST_CASE("MBC30 selects all eight ROM bank bits")
{
    Cartridge cartridge(make_rom(256, 0x11, 0x07));

    cartridge.write(0x2000, 0x80);
    REQUIRE(cartridge.read(0x4000) == 0x80);

    cartridge.write(0x2000, 0xFF);
    REQUIRE(cartridge.read(0x4000) == 0xFF);
}

TEST_CASE("MBC30 selects eight RAM banks alongside the RTC")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x05));

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x07);
    cartridge.write(0xA000, 0x77);
    cartridge.write(0x4000, 0x03);
    cartridge.write(0xA000, 0x33);

    cartridge.write(0x4000, 0x07);
    REQUIRE(cartridge.read(0xA000) == 0x77);
    cartridge.write(0x4000, 0x03);
    REQUIRE(cartridge.read(0xA000) == 0x33);
}

TEST_CASE("RTC MBC3 leaves RAM selections four through seven unmapped")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x00);
    cartridge.write(0xA000, 0x20);
    cartridge.write(0x4000, 0x04);
    cartridge.write(0xA000, 0x44);

    REQUIRE(cartridge.read(0xA000) == 0xFF);

    cartridge.write(0x4000, 0x00);
    REQUIRE(cartridge.read(0xA000) == 0x20);
}

TEST_CASE("MBC3 without an RTC masks RAM bank selection to two bits")
{
    Cartridge cartridge(make_rom(8, 0x13, 0x02, 0x03));

    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x07);
    cartridge.write(0xA000, 0x37);
    cartridge.write(0x4000, 0x03);

    REQUIRE(cartridge.read(0xA000) == 0x37);
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

    cartridge.advance_rtc_milliseconds(5'000);
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

    cartridge.advance_rtc_milliseconds(2'000);
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

TEST_CASE("MBC3 RTC invalid counter overflow does not carry")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 63);
    cartridge.write(0x4000, 0x09);
    cartridge.write(0xA000, 10);

    cartridge.advance_rtc_milliseconds(1'000);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);

    cartridge.write(0x4000, 0x08);
    REQUIRE(cartridge.read(0xA000) == 0);
    cartridge.write(0x4000, 0x09);
    REQUIRE(cartridge.read(0xA000) == 10);
}

TEST_CASE("MBC3 RTC normalizes invalid counters before bulk catch-up")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 58);
    cartridge.write(0x4000, 0x09);
    cartridge.write(0xA000, 63);

    cartridge.advance_rtc_milliseconds(3'000);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);

    cartridge.write(0x4000, 0x08);
    REQUIRE(cartridge.read(0xA000) == 1);
    cartridge.write(0x4000, 0x09);
    REQUIRE(cartridge.read(0xA000) == 0);
}

TEST_CASE("MBC3 RTC seconds writes reset only the sub-second phase")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 10);

    cartridge.advance_rtc_milliseconds(500);
    cartridge.write(0x4000, 0x09);
    cartridge.write(0xA000, 20);
    cartridge.advance_rtc_milliseconds(500);

    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    cartridge.write(0x4000, 0x08);
    REQUIRE(cartridge.read(0xA000) == 11);

    cartridge.write(0xA000, 30);
    cartridge.advance_rtc_milliseconds(999);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    REQUIRE(cartridge.read(0xA000) == 30);

    cartridge.advance_rtc_milliseconds(1);
    cartridge.write(0x6000, 0x00);
    cartridge.write(0x6000, 0x01);
    REQUIRE(cartridge.read(0xA000) == 31);
}

TEST_CASE("MBC3 RTC does not advance while halted")
{
    Cartridge cartridge(make_rom(8, 0x10, 0x02, 0x03));
    cartridge.write(0x0000, 0x0A);
    cartridge.write(0x4000, 0x08);
    cartridge.write(0xA000, 20);
    cartridge.write(0x4000, 0x0C);
    cartridge.write(0xA000, 0x40);

    cartridge.advance_rtc_milliseconds(30'000);
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
