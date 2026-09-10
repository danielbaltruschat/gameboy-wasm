#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct CartridgeHeader {
    uint8_t cartridgeType = 0;       // 0x0147: describes the cartridge PCB.
    std::size_t ramSizeBytes = 0;    // Decoded from 0x0149.
};

enum class MapperType {
    RomOnly,
    Mbc1,
    Mbc2,
    Mbc3,
    Mbc5,
    Unknown,
};

struct CartridgeCapabilities {
    MapperType mapper = MapperType::Unknown;
    bool hasRam = false;
    bool hasBattery = false;
    bool hasTimer = false;
    bool hasRumble = false;
    bool hasMbc1MulticartLayout = false;
};

struct Mbc1State {
    uint8_t lower_rom_bank_bits = 1;
    uint8_t upper_bank_bits = 0;
    uint8_t banking_mode = 0;
};

struct Mbc2State {
    std::array<uint8_t, 512> ram{};
};

struct Mbc3Rtc {
    uint32_t subsecond_dots = 0;
    uint8_t seconds = 0;
    uint8_t minutes = 0;
    uint8_t hours = 0;
    uint16_t days = 0;
    bool halted = false;
    bool day_carry = false;

    uint8_t latched_seconds = 0;
    uint8_t latched_minutes = 0;
    uint8_t latched_hours = 0;
    uint16_t latched_days = 0;
    bool latched_halted = false;
    bool latched_day_carry = false;
    bool latch_armed = false;
};

struct Mbc5State {
    uint16_t rom_bank = 1; // 9-bit value.
    uint8_t ram_bank = 0;
    bool rumble_enabled = false;
};

class Cartridge {
public:
    Cartridge() = default;
    explicit Cartridge(std::vector<uint8_t> rom);

    void load_rom(std::span<const uint8_t> data);
    void reset_mapper();
    void tick_rtc_dots(uint32_t dots);
    void tick_rtc_seconds(uint32_t seconds);

    uint8_t read(uint16_t address) const;
    std::optional<uint8_t> read_bus(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    const CartridgeHeader& get_header() const;
    const CartridgeCapabilities& get_capabilities() const;
    bool loaded() const;

private:
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;
    CartridgeHeader header;
    CartridgeCapabilities capabilities;

    bool ram_enabled = false;
    uint16_t rom_bank = 1;
    uint8_t ram_bank = 0;
    uint8_t rtc_register_select = 0;
    bool mbc30 = false;

    Mbc1State mbc1;
    Mbc2State mbc2;
    Mbc3Rtc mbc3_rtc;
    Mbc5State mbc5;

    void parse_header();
    void configure_mapper();
    std::size_t effective_rom_offset(uint16_t address) const;
    std::size_t effective_ram_offset(uint16_t address) const;

    std::optional<uint8_t> read_external_ram(uint16_t address) const;
    void write_external_ram(uint16_t address, uint8_t value);
    uint8_t read_rtc_register() const;
    void write_rtc_register(uint8_t value);
};
