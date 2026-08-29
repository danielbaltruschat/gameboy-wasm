#include "bus.h"
#include "apu.h"
#include "cartridge.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "serial.h"
#include "timer.h"

uint8_t MemBus::read(uint16_t addr) {
    notify_cpu_address_bus(addr, BusAccessType::Read);

    if (oam_dma.is_active() && oam_dma.blocks_cpu_access(addr))
        return open_bus_value;

    uint8_t value = read_unchecked(addr);
    update_open_bus(value);

    return value;
}

void MemBus::write(uint16_t addr, uint8_t value) {
    notify_cpu_address_bus(addr, BusAccessType::Write);

    if (oam_dma.is_active() && oam_dma.blocks_cpu_access(addr)) {
        return;
    }

    write_unchecked(addr, value);
    update_open_bus(value);
}

uint8_t MemBus::read_dma_source(uint16_t addr) { return read_unchecked(addr); }

void MemBus::update_open_bus(uint8_t value) { open_bus_value = value; }

void MemBus::notify_cpu_address_bus(uint16_t addr, BusAccessType access_type) {
    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        ppu.notify_oam_bus_access(addr, access_type);
    }
}

bool MemBus::dma_active() const {
    return oam_dma.is_active();
}

void MemBus::tick_dma_dots(int dots) {
    const bool was_active = oam_dma.is_active();

    oam_dma.tick_dots(dots);

    if (oam_dma.is_active() != was_active) {
        ppu.set_oam_dma_active(oam_dma.is_active());
    }

    while (oam_dma.copy_pending()) {
        uint8_t value = read_dma_source(oam_dma.source_addr()); //bypass DMA blocking
        ppu.write_oam_dma(oam_dma.oam_offset(), value);
        oam_dma.acknowledge_copy();
    }

    ppu.set_oam_dma_active(oam_dma.is_active());

}
