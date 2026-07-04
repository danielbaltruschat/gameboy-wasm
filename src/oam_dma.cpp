#include "oam_dma.h"
#include <cstdint>

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
