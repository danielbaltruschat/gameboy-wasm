#include "oam_dma.h"
#include <cassert>
#include <cstdint>
#include <sys/types.h>

void OamDma::reset() {
    active = false;
    start_pending = false;
    restart_ready = false;
    restart_warmup = false;
    dma_reg = 0x00;
    source_base = 0x0000;
    pending_source_base = 0x0000;
    index = 0;
    dot_counter = 0;
    start_dots_remaining = 0;
    completion_dots_remaining = 0;
    pending_copies = 0;
}

bool OamDma::is_active() const { return active; }

uint8_t OamDma::read_reg() const {return dma_reg;}

bool OamDma::copy_pending() const {return pending_copies > 0;}

int OamDma::pending_copy_count() const { return pending_copies; }

uint16_t OamDma::source_addr() const { return source_base + index; }

uint16_t OamDma::oam_offset() const { return index; }

bool OamDma::blocks_cpu_access(uint16_t addr) const {
    if (!active) {
        return false;
    }

    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        return index > 0 || restart_warmup;
    }

    if (index == 0) {
        return false;
    }

    if (addr >= 0xFF00) {
        return false;
    }

    const bool address_uses_vram_bus = addr >= 0x8000 && addr <= 0x9FFF;
    return address_uses_vram_bus == uses_vram_bus();
}

bool OamDma::uses_vram_bus() const {
    const uint8_t source_high_byte = static_cast<uint8_t>(source_base >> 8);
    return source_high_byte >= 0x80 && source_high_byte <= 0x9F;
}

uint16_t OamDma::conflict_addr() const {
    assert(active);
    assert(index > 0);
    return static_cast<uint16_t>(source_base + index - 1);
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
        completion_dots_remaining = 0;
        pending_copies = 0;
    }
}

void OamDma::acknowledge_copy() {
    if (pending_copies <= 0) {
        return;
    }

    pending_copies--;
    index++;
    restart_warmup = false;

    if (restart_ready && pending_copies == 0) {
        begin_pending_transfer();
        return;
    }

    if (index >= 160) {
        pending_copies = 0;
        dot_counter = 0;
        completion_dots_remaining = 4;
    }
}

void OamDma::tick_dots(int dots) {
    if (dots <= 0) return;

    for (int dot = 0; dot < dots; ++dot) {
        if (completion_dots_remaining > 0) {
            completion_dots_remaining--;
            if (completion_dots_remaining == 0) {
                active = false;
            }
        } else if (active) {
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
    const bool was_active = active;
    active = true;
    start_pending = false;
    restart_ready = false;
    source_base = pending_source_base;
    index = 0;
    dot_counter = 0;
    completion_dots_remaining = 0;
    pending_copies = 0;
    restart_warmup = was_active;
}
