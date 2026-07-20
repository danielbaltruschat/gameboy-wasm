#include "cartridge.h"

#include <cassert>
#include <utility>

namespace {

constexpr std::size_t minimum_header_size = 0x150;
constexpr std::size_t rom_bank_size = 0x4000;
constexpr std::size_t ram_bank_size = 0x2000;

} // namespace

Cartridge::Cartridge(std::vector<uint8_t> rom)
    : rom(std::move(rom)) {
    assert(this->rom.size() >= minimum_header_size);
    parse_header();
    configure_mapper();
    reset_mapper();
}

void Cartridge::load_rom(std::span<const uint8_t> rom) {
    assert(rom.size() >= minimum_header_size);

    this->rom.assign(rom.begin(), rom.end());

    parse_header();
    configure_mapper();
    reset_mapper();
}

bool Cartridge::loaded() const {
    return !rom.empty();
}

const CartridgeHeader& Cartridge::get_header() const {
    return header;
}

const CartridgeCapabilities& Cartridge::get_capabilities() const {
    return capabilities;
}

void Cartridge::parse_header() {
    header = {};
    header.cartridgeType = rom[0x0147];

    switch (rom[0x0149]) {
    case 0x02:
        header.ramSizeBytes = 8 * 1024;
        break;
    case 0x03:
        header.ramSizeBytes = 32 * 1024;
        break;
    case 0x04:
        header.ramSizeBytes = 128 * 1024;
        break;
    case 0x05:
        header.ramSizeBytes = 64 * 1024;
        break;
    default:
        header.ramSizeBytes = 0;
        break;
    }
}

void Cartridge::configure_mapper() {
    capabilities = {};

    switch (header.cartridgeType) {
    case 0x00:
        capabilities.mapper = MapperType::RomOnly;
        break;
    case 0x08:
        capabilities.mapper = MapperType::RomOnly;
        capabilities.hasRam = true;
        break;
    case 0x09:
        capabilities.mapper = MapperType::RomOnly;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        break;
    case 0x01:
        capabilities.mapper = MapperType::Mbc1;
        break;
    case 0x02:
        capabilities.mapper = MapperType::Mbc1;
        capabilities.hasRam = true;
        break;
    case 0x03:
        capabilities.mapper = MapperType::Mbc1;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        break;
    case 0x05:
        capabilities.mapper = MapperType::Mbc2;
        capabilities.hasRam = true;
        break;
    case 0x06:
        capabilities.mapper = MapperType::Mbc2;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        break;
    case 0x0F:
        capabilities.mapper = MapperType::Mbc3;
        capabilities.hasBattery = true;
        capabilities.hasTimer = true;
        break;
    case 0x10:
        capabilities.mapper = MapperType::Mbc3;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        capabilities.hasTimer = true;
        break;
    case 0x11:
        capabilities.mapper = MapperType::Mbc3;
        break;
    case 0x12:
        capabilities.mapper = MapperType::Mbc3;
        capabilities.hasRam = true;
        break;
    case 0x13:
        capabilities.mapper = MapperType::Mbc3;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        break;
    case 0x19:
        capabilities.mapper = MapperType::Mbc5;
        break;
    case 0x1A:
        capabilities.mapper = MapperType::Mbc5;
        capabilities.hasRam = true;
        break;
    case 0x1B:
        capabilities.mapper = MapperType::Mbc5;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        break;
    case 0x1C:
        capabilities.mapper = MapperType::Mbc5;
        capabilities.hasRumble = true;
        break;
    case 0x1D:
        capabilities.mapper = MapperType::Mbc5;
        capabilities.hasRam = true;
        capabilities.hasRumble = true;
        break;
    case 0x1E:
        capabilities.mapper = MapperType::Mbc5;
        capabilities.hasRam = true;
        capabilities.hasBattery = true;
        capabilities.hasRumble = true;
        break;
    default:
        capabilities.mapper = MapperType::Unknown;
        break;
    }

    if (capabilities.hasRam && capabilities.mapper != MapperType::Mbc2) {
        ram.assign(header.ramSizeBytes, 0);
    } else {
        ram.clear();
    }

    mbc2 = {};
    mbc3_rtc = {};
}

void Cartridge::reset_mapper() {
    ram_enabled = false;
    rom_bank = 1;
    ram_bank = 0;
    rtc_register_select = 0;

    mbc1 = {};
    mbc3_rtc.latch_armed = false;
    mbc5 = {};
}

uint8_t Cartridge::read(uint16_t address) const {
    if (!loaded()) {
        return 0xFF;
    }

    if (address <= 0x7FFF) {
        const std::size_t offset = effective_rom_offset(address);
        return offset < rom.size() ? rom[offset] : 0xFF;
    }

    if (address >= 0xA000 && address <= 0xBFFF) {
        return read_external_ram(address);
    }

    return 0xFF;
}

void Cartridge::write(uint16_t address, uint8_t value) {
    if (!loaded()) {
        return;
    }

    if (address >= 0xA000 && address <= 0xBFFF) {
        write_external_ram(address, value);
        return;
    }

    if (address > 0x7FFF) {
        return;
    }

    switch (capabilities.mapper) {
    case MapperType::RomOnly:
        break;
    case MapperType::Mbc1:
        if (address <= 0x1FFF) {
            ram_enabled = (value & 0x0F) == 0x0A;
        } else if (address <= 0x3FFF) {
            mbc1.lower_rom_bank_bits = value & 0x1F;
            if (mbc1.lower_rom_bank_bits == 0) {
                mbc1.lower_rom_bank_bits = 1;
            }
        } else if (address <= 0x5FFF) {
            mbc1.upper_bank_bits = value & 0x03;
        } else {
            mbc1.banking_mode = value & 0x01;
        }
        break;
    case MapperType::Mbc2:
        if (address <= 0x3FFF) {
            if ((address & 0x0100) == 0) {
                ram_enabled = (value & 0x0F) == 0x0A;
            } else {
                rom_bank = value & 0x0F;
                if (rom_bank == 0) {
                    rom_bank = 1;
                }
            }
        }
        break;
    case MapperType::Mbc3:
        if (address <= 0x1FFF) {
            ram_enabled = (value & 0x0F) == 0x0A;
        } else if (address <= 0x3FFF) {
            rom_bank = value & 0x7F;
            if (rom_bank == 0) {
                rom_bank = 1;
            }
        } else if (address <= 0x5FFF) {
            rtc_register_select = value;
            if (value <= 0x07) {
                ram_bank = value;
            }
        } else if (value == 0x00) {
            mbc3_rtc.latch_armed = true;
        } else {
            if (value == 0x01 && mbc3_rtc.latch_armed) {
                mbc3_rtc.latched_seconds = mbc3_rtc.seconds;
                mbc3_rtc.latched_minutes = mbc3_rtc.minutes;
                mbc3_rtc.latched_hours = mbc3_rtc.hours;
                mbc3_rtc.latched_days = mbc3_rtc.days;
                mbc3_rtc.latched_halted = mbc3_rtc.halted;
                mbc3_rtc.latched_day_carry = mbc3_rtc.day_carry;
            }
            mbc3_rtc.latch_armed = false;
        }
        break;
    case MapperType::Mbc5:
        if (address <= 0x1FFF) {
            ram_enabled = (value & 0x0F) == 0x0A;
        } else if (address <= 0x2FFF) {
            mbc5.rom_bank = static_cast<uint16_t>((mbc5.rom_bank & 0x0100) | value);
        } else if (address <= 0x3FFF) {
            mbc5.rom_bank = static_cast<uint16_t>(
                (mbc5.rom_bank & 0x00FF) |
                (static_cast<uint16_t>(value & 0x01) << 8)
            );
        } else if (address <= 0x5FFF) {
            if (capabilities.hasRumble) {
                mbc5.rumble_enabled = (value & 0x08) != 0;
                mbc5.ram_bank = value & 0x07;
            } else {
                mbc5.ram_bank = value & 0x0F;
            }
        }
        break;
    case MapperType::Unknown:
        break;
    }
}

std::size_t Cartridge::effective_rom_offset(uint16_t address) const {
    assert(address <= 0x7FFF);

    if (rom.empty()) {
        return 0;
    }

    if (capabilities.mapper == MapperType::RomOnly) {
        return address;
    }

    if (capabilities.mapper == MapperType::Unknown) {
        return rom.size();
    }

    std::size_t bank = 0;

    if (address >= 0x4000) {
        switch (capabilities.mapper) {
        case MapperType::Mbc1:
            bank =
                (static_cast<std::size_t>(mbc1.upper_bank_bits) << 5) |
                mbc1.lower_rom_bank_bits;
            break;
        case MapperType::Mbc2:
        case MapperType::Mbc3:
            bank = rom_bank;
            break;
        case MapperType::Mbc5:
            bank = mbc5.rom_bank;
            break;
        case MapperType::RomOnly:
        case MapperType::Unknown:
            break;
        }
    } else if (capabilities.mapper == MapperType::Mbc1 && mbc1.banking_mode != 0) {
        bank = static_cast<std::size_t>(mbc1.upper_bank_bits) << 5;
    }

    const std::size_t bank_count = (rom.size() + rom_bank_size - 1) / rom_bank_size;
    bank %= bank_count;

    return bank * rom_bank_size + (address & (rom_bank_size - 1));
}

std::size_t Cartridge::effective_ram_offset(uint16_t address) const {
    assert(address >= 0xA000 && address <= 0xBFFF);

    if (ram.empty()) {
        return 0;
    }

    std::size_t bank = 0;

    switch (capabilities.mapper) {
    case MapperType::Mbc1:
        if (mbc1.banking_mode != 0) {
            bank = mbc1.upper_bank_bits;
        }
        break;
    case MapperType::Mbc3:
        bank = ram_bank;
        break;
    case MapperType::Mbc5:
        bank = mbc5.ram_bank;
        break;
    case MapperType::RomOnly:
    case MapperType::Mbc2:
    case MapperType::Unknown:
        break;
    }

    return (bank * ram_bank_size + (address - 0xA000)) % ram.size();
}

uint8_t Cartridge::read_external_ram(uint16_t address) const {
    assert(address >= 0xA000 && address <= 0xBFFF);

    switch (capabilities.mapper) {
    case MapperType::RomOnly:
        if (ram.empty()) {
            return 0xFF;
        }
        return ram[effective_ram_offset(address)];
    case MapperType::Mbc2:
        if (!ram_enabled) {
            return 0xFF;
        }
        return 0xF0 | (mbc2.ram[(address - 0xA000) & 0x01FF] & 0x0F);
    case MapperType::Mbc3:
        if (!ram_enabled) {
            return 0xFF;
        }
        if (rtc_register_select >= 0x08 && rtc_register_select <= 0x0C) {
            return capabilities.hasTimer ? read_rtc_register() : 0xFF;
        }
        if (rtc_register_select > 0x07 || ram.empty()) {
            return 0xFF;
        }
        return ram[effective_ram_offset(address)];
    case MapperType::Mbc1:
    case MapperType::Mbc5:
        if (!ram_enabled || ram.empty()) {
            return 0xFF;
        }
        return ram[effective_ram_offset(address)];
    case MapperType::Unknown:
        return 0xFF;
    }

    return 0xFF;
}

void Cartridge::write_external_ram(uint16_t address, uint8_t value) {
    assert(address >= 0xA000 && address <= 0xBFFF);

    switch (capabilities.mapper) {
    case MapperType::RomOnly:
        if (!ram.empty()) {
            ram[effective_ram_offset(address)] = value;
        }
        break;
    case MapperType::Mbc2:
        if (ram_enabled) {
            mbc2.ram[(address - 0xA000) & 0x01FF] = value & 0x0F;
        }
        break;
    case MapperType::Mbc3:
        if (!ram_enabled) {
            break;
        }
        if (rtc_register_select >= 0x08 && rtc_register_select <= 0x0C) {
            if (capabilities.hasTimer) {
                write_rtc_register(value);
            }
        } else if (rtc_register_select <= 0x07 && !ram.empty()) {
            ram[effective_ram_offset(address)] = value;
        }
        break;
    case MapperType::Mbc1:
    case MapperType::Mbc5:
        if (ram_enabled && !ram.empty()) {
            ram[effective_ram_offset(address)] = value;
        }
        break;
    case MapperType::Unknown:
        break;
    }
}

uint8_t Cartridge::read_rtc_register() const {
    switch (rtc_register_select) {
    case 0x08:
        return mbc3_rtc.latched_seconds;
    case 0x09:
        return mbc3_rtc.latched_minutes;
    case 0x0A:
        return mbc3_rtc.latched_hours;
    case 0x0B:
        return mbc3_rtc.latched_days & 0x00FF;
    case 0x0C:
        return
            ((mbc3_rtc.latched_days >> 8) & 0x01) |
            (mbc3_rtc.latched_halted ? 0x40 : 0x00) |
            (mbc3_rtc.latched_day_carry ? 0x80 : 0x00);
    default:
        return 0xFF;
    }
}

void Cartridge::write_rtc_register(uint8_t value) {
    switch (rtc_register_select) {
    case 0x08:
        mbc3_rtc.seconds = value & 0x3F;
        break;
    case 0x09:
        mbc3_rtc.minutes = value & 0x3F;
        break;
    case 0x0A:
        mbc3_rtc.hours = value & 0x1F;
        break;
    case 0x0B:
        mbc3_rtc.days = static_cast<uint16_t>((mbc3_rtc.days & 0x0100) | value);
        break;
    case 0x0C:
        mbc3_rtc.days = static_cast<uint16_t>(
            (mbc3_rtc.days & 0x00FF) |
            (static_cast<uint16_t>(value & 0x01) << 8)
        );
        mbc3_rtc.halted = (value & 0x40) != 0;
        mbc3_rtc.day_carry = (value & 0x80) != 0;
        break;
    default:
        break;
    }
}

void Cartridge::tick_rtc_seconds(uint32_t seconds) {
    if (
        capabilities.mapper != MapperType::Mbc3 ||
        !capabilities.hasTimer ||
        mbc3_rtc.halted ||
        seconds == 0
    ) {
        return;
    }

    uint64_t total = static_cast<uint64_t>(mbc3_rtc.seconds) + seconds;
    mbc3_rtc.seconds = total % 60;

    total = static_cast<uint64_t>(mbc3_rtc.minutes) + total / 60;
    mbc3_rtc.minutes = total % 60;

    total = static_cast<uint64_t>(mbc3_rtc.hours) + total / 60;
    mbc3_rtc.hours = total % 24;

    total = static_cast<uint64_t>(mbc3_rtc.days) + total / 24;
    if (total > 0x01FF) {
        mbc3_rtc.day_carry = true;
    }
    mbc3_rtc.days = total & 0x01FF;
}
