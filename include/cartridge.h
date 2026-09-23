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
    uint16_t subsecond_ticks = 0;
    uint16_t subsecond_remainder = 0;
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

// Battery-backed MBC3 RTC state. Latch and mapper selection state are volatile.
struct Mbc3RtcRegisters {
    uint16_t subsecond_ticks = 0;
    uint16_t subsecond_remainder = 0;
    uint8_t seconds = 0;
    uint8_t minutes = 0;
    uint8_t hours = 0;
    uint16_t days = 0;
    bool halted = false;
    bool day_carry = false;
};

struct Mbc5State {
    uint16_t rom_bank = 1; // 9-bit value.
    uint8_t ram_bank = 0;
    bool rumble_enabled = false;
};

class Cartridge {
public:
    using RtcClock = uint64_t (*)(void* context);

    Cartridge() = default;
    explicit Cartridge(std::vector<uint8_t> rom);

    void load_rom(std::span<const uint8_t> data);
    void reset_mapper();
    void set_rtc_clock(RtcClock clock, void* context);
    void advance_rtc_milliseconds(uint64_t milliseconds);

    bool has_battery() const;
    bool has_rtc() const;
    bool battery_dirty() const;
    uint64_t battery_revision() const;
    std::vector<uint8_t> battery_ram();
    std::vector<uint8_t> take_battery_ram();
    bool load_battery_ram(std::span<const uint8_t> data);
    Mbc3RtcRegisters rtc_registers();
    bool load_rtc_registers(const Mbc3RtcRegisters& registers);

    uint8_t read(uint16_t address);
    std::optional<uint8_t> read_bus(uint16_t address);
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

    RtcClock rtc_clock = nullptr;
    void* rtc_clock_context = nullptr;
    uint64_t rtc_clock_anchor = 0;
    bool rtc_clock_anchored = false;
    bool persistent_dirty = false;
    uint64_t persistent_revision = 0;

    void parse_header();
    void configure_mapper();
    std::size_t effective_rom_offset(uint16_t address) const;
    std::size_t effective_ram_offset(uint16_t address) const;

    std::optional<uint8_t> read_external_ram(uint16_t address);
    void write_external_ram(uint16_t address, uint8_t value);
    uint8_t read_rtc_register();
    void write_rtc_register(uint8_t value);
    void sync_rtc();
    void advance_rtc_milliseconds_internal(uint64_t milliseconds);
    void advance_rtc_seconds(uint64_t seconds);
    void mark_persistent_dirty();
    std::size_t battery_ram_size() const;
};
