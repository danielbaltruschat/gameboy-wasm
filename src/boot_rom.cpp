#include "boot_rom.h"

#include <algorithm>
#include <cstdint>
#include <cassert>

void BootRom::load_dmg(std::span<const uint8_t> rom_data) {
    assert(rom_data.size() == dmg_size);

    std::copy(rom_data.begin(), rom_data.end(), data.begin());
    has_data = true;
    is_mapped = true;
    disable_register = 0;
}

void BootRom::reset() {
    is_mapped = true;
    disable_register = 0;
}

bool BootRom::present() const {
    return has_data;
}

bool BootRom::mapped() const {
    return has_data && is_mapped;
}

uint8_t BootRom::read(uint16_t addr) const {
    assert(addr <= 0xFF);
    return data[addr];
}

uint8_t BootRom::read_disable_register() const {
    return disable_register;
}

void BootRom::write_disable_register(uint8_t value) {
    disable_register = value;
    if (value != 0) is_mapped = false;
}
