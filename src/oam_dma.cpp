#include "oam_dma.h"
#include <cstdint>
#include <sys/types.h>

void OamDma::reset() {
    active = false;
    dma_reg = 0x00;
    source_base = 0x0000;
    index = 0;
    dot_counter = 0;
    pending_copies = 0;
}

bool OamDma::is_active() const { return active; }

uint8_t OamDma::read_reg() const {return dma_reg;}

bool OamDma::copy_pending() const {return pending_copies > 0;}

int OamDma::pending_copy_count() const { return pending_copies; }

uint16_t OamDma::source_addr() const { return source_base + index; }

uint16_t OamDma::oam_offset() const { return index; }

bool OamDma::blocks_cpu_access(uint16_t addr) const {
    return active && !(addr >= 0xFF80 && addr <= 0xFFFE); //address for HRAM allowed during blocked phase
}

void OamDma::start(uint8_t source_high_byte) {
    active = true;
    dma_reg = source_high_byte;
    source_base = static_cast<uint16_t>(source_high_byte) << 8;
    index = 0;
    dot_counter = 0;
    pending_copies = 0;
}

void OamDma::acknowledge_copy() {
    if (pending_copies <= 0) {
        return;
    }

    pending_copies--;
    index++;

    if (index >= 160) {
        active = false;
        pending_copies = 0;
        dot_counter = 0;
    }
}

void OamDma::tick_dots(int dots) {
    if (!active || dots <= 0) return;

    dot_counter += dots;

    while (dot_counter >= 4 && (index + pending_copies) < 160) { // OAM DMA copies 1 byte per M cycle (so 4 dot cycle)
        dot_counter -= 4;
        pending_copies++;
    }
}
