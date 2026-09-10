#include "bus.h"
#include "cartridge.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

void MemBus::reset() {
    oam_dma.reset();
    ppu.set_oam_dma_active(false);
    open_bus_value = 0xFF;
}

uint8_t MemBus::read(uint16_t addr) {
    notify_cpu_address_bus(addr, BusAccessType::Read);

    if (addr == 0xFF46) {
        return oam_dma.read_reg();
    }

    if (oam_dma.is_active() && oam_dma.blocks_cpu_access(addr))
        return read_blocked_by_dma(addr);

    uint8_t value = read_unchecked(addr);
    if (addr > 0x00FF || !boot_rom.mapped()) {
        update_open_bus(addr, value);
    }

    return value;
}

uint8_t MemBus::read_and_internal(uint16_t addr) {
    notify_cpu_address_bus(addr, BusAccessType::ReadAndInternal);

    if (addr == 0xFF46) {
        return oam_dma.read_reg();
    }

    if (oam_dma.is_active() && oam_dma.blocks_cpu_access(addr)) {
        return read_blocked_by_dma(addr);
    }

    const uint8_t value = read_unchecked(addr);
    if (addr > 0x00FF || !boot_rom.mapped()) {
        update_open_bus(addr, value);
    }
    return value;
}

void MemBus::write(uint16_t addr, uint8_t value) {
    notify_cpu_address_bus(addr, BusAccessType::Write);
    update_open_bus(addr, value);

    if (addr != 0xFF46 && oam_dma.is_active() && oam_dma.blocks_cpu_access(addr)) {
        if (addr < 0xFE00) {
            write_blocked_by_dma(value);
        }
        return;
    }

    write_unchecked(addr, value);
}

void MemBus::write_cpu_stat(uint8_t value) {
    constexpr uint16_t address = 0xFF41;
    notify_cpu_address_bus(address, BusAccessType::Write);
    update_open_bus(address, value);

    if (oam_dma.is_active() && oam_dma.blocks_cpu_access(address)) {
        write_blocked_by_dma(value);
        return;
    }

    ppu.write_cpu_stat(value);
}

void MemBus::internal_cycle(uint16_t addr) {
    notify_cpu_address_bus(addr, BusAccessType::Internal);
}

uint8_t MemBus::read_dma_source(uint16_t addr) {
    uint8_t value = 0xFF;

    if (addr <= 0x7FFF) {
        value = cartridge.read_bus(addr).value_or(open_bus_value);
    } else if (addr <= 0x9FFF) {
        value = ppu.read_vram_dma(addr);
    } else if (addr <= 0xBFFF) {
        value = cartridge.read_bus(addr).value_or(open_bus_value);
    } else if (addr <= 0xDFFF) {
        value = memory.read_wram(addr);
    } else {
        value = memory.read_wram(addr - 0x2000);
    }

    update_open_bus(addr, value);
    return value;
}

void MemBus::update_open_bus(uint16_t addr, uint8_t value) {
    if (addr < 0xFE00 && !(addr >= 0x8000 && addr <= 0x9FFF)) {
        open_bus_value = value;
    }
}

void MemBus::notify_cpu_address_bus(uint16_t addr, BusAccessType access_type) {
    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        ppu.notify_oam_bus_access(addr, access_type);
    }
}

bool MemBus::dma_active() const {
    return oam_dma.is_active();
}

void MemBus::tick_dma_dots(int dots) {
    oam_dma.tick_dots(dots);
    ppu.set_oam_dma_active(oam_dma.is_active(), oam_dma.oam_offset());

    while (oam_dma.copy_pending()) {
        uint8_t value = read_dma_source(oam_dma.source_addr()); //bypass DMA blocking
        ppu.write_oam_dma(oam_dma.oam_offset(), value);
        oam_dma.acknowledge_copy();
    }

    ppu.set_oam_dma_active(oam_dma.is_active(), oam_dma.oam_offset());
}

uint8_t MemBus::read_unchecked(uint16_t addr) {
    if (addr <= 0x7FFF) {
        if (addr <= 0x00FF && boot_rom.mapped()) {
            return boot_rom.read(addr);
        }
        return cartridge.read_bus(addr).value_or(open_bus_value);
    }
    if (addr <= 0x9FFF) return ppu.read(addr);
    if (addr <= 0xBFFF) return cartridge.read_bus(addr).value_or(open_bus_value);
    if (addr <= 0xDFFF) return memory.read_wram(addr);
    if (addr <= 0xFDFF) return memory.read_wram(addr - 0x2000);
    if (addr <= 0xFEFF) return ppu.read(addr);

    switch (addr) {
    case 0xFF00: return joypad.read();
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        return timer.read(addr);
    case 0xFF0F: return interrupts.read_if();
    case 0xFF40:
    case 0xFF41:
    case 0xFF42:
    case 0xFF43:
    case 0xFF44:
    case 0xFF45:
    case 0xFF47:
    case 0xFF48:
    case 0xFF49:
    case 0xFF4A:
    case 0xFF4B:
        return ppu.read(addr);
    case 0xFF46: return oam_dma.read_reg();
    case 0xFFFF: return interrupts.read_ie();
    default:
        if (addr >= 0xFF80 && addr <= 0xFFFE) {
            return memory.read_hram(addr);
        }
        return 0xFF;
    }
}

void MemBus::write_unchecked(uint16_t addr, uint8_t value) {
    if (addr <= 0x7FFF) {
        cartridge.write(addr, value);
        return;
    }
    if (addr <= 0x9FFF) {
        ppu.write(addr, value);
        return;
    }
    if (addr <= 0xBFFF) {
        cartridge.write(addr, value);
        return;
    }
    if (addr <= 0xDFFF) {
        memory.write_wram(addr, value);
        return;
    }
    if (addr <= 0xFDFF) {
        memory.write_wram(addr - 0x2000, value);
        return;
    }
    if (addr <= 0xFE9F) {
        ppu.write(addr, value);
        return;
    }
    if (addr <= 0xFEFF) return;

    switch (addr) {
    case 0xFF00:
        joypad.write(value);
        break;
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        timer.write(addr, value);
        break;
    case 0xFF0F:
        interrupts.write_if(value);
        break;
    case 0xFF40:
    case 0xFF41:
    case 0xFF42:
    case 0xFF43:
    case 0xFF44:
    case 0xFF45:
    case 0xFF47:
    case 0xFF48:
    case 0xFF49:
    case 0xFF4A:
    case 0xFF4B:
        ppu.write(addr, value);
        break;
    case 0xFF46:
        oam_dma.start(value);
        break;
    case 0xFF50:
        boot_rom.write_disable_register(value);
        break;
    case 0xFFFF:
        interrupts.write_ie(value);
        break;
    default:
        if (addr >= 0xFF80 && addr <= 0xFFFE) {
            memory.write_hram(addr, value);
        }
        break;
    }
}

uint8_t MemBus::read_blocked_by_dma(uint16_t addr) {
    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        return 0xFF;
    }
    return read_dma_source(oam_dma.conflict_addr());
}

void MemBus::write_blocked_by_dma(uint8_t value) {
    const uint16_t addr = oam_dma.conflict_addr();

    if (addr < 0xA000) {
        write_unchecked(addr, value);
    } else {
        ppu.write_oam_dma_conflict(value);
    }
}
