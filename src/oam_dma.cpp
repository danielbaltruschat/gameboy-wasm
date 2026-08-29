#include "oam_dma.h"
#include <cstdint>
#include <sys/types.h>

void OamDma::reset() {
    active = false;
    start_pending = false;
    restart_ready = false;
    dma_reg = 0x00;
    source_base = 0x0000;
    pending_source_base = 0x0000;
    index = 0;
    dot_counter = 0;
    start_dots_remaining = 0;
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
    dma_reg = source_high_byte;
    pending_source_base = static_cast<uint16_t>(source_high_byte) << 8;
    start_pending = true;
    restart_ready = false;
    start_dots_remaining = 4;

    if (!active) {
        index = 0;
        dot_counter = 0;
        pending_copies = 0;
    }
}

void OamDma::acknowledge_copy() {
    if (pending_copies <= 0) {
        return;
    }

    pending_copies--;
    index++;

    if (restart_ready && pending_copies == 0) {
        begin_pending_transfer();
        return;
    }

    if (index >= 160) {
        active = false;
        pending_copies = 0;
        dot_counter = 0;
    }
}

void OamDma::tick_dots(int dots) {
    if (dots <= 0) return;

    for (int dot = 0; dot < dots; ++dot) {
        if (active) {
            dot_counter++;
            if (dot_counter == 4 && (index + pending_copies) < 160) {
                dot_counter = 0;
                pending_copies++;
            }
        }

        if (!start_pending || restart_ready) {
            continue;
        }

        start_dots_remaining--;
        if (start_dots_remaining == 0) {
            if (active && pending_copies > 0) {
                restart_ready = true;
            } else {
                begin_pending_transfer();
            }
        }
    }
}

void OamDma::begin_pending_transfer() {
    active = true;
    start_pending = false;
    restart_ready = false;
    source_base = pending_source_base;
    index = 0;
    dot_counter = 0;
    pending_copies = 0;
}
