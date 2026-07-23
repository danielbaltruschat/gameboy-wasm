#include "ppu.h"
#include "interrupt_controller.h"
#include <cstdint>
#include <cassert>

void Framebuffer::set_pixel(int x, int y, uint32_t rgba) {
    pixels_[y * width + x] = rgba;
}

const uint32_t* Framebuffer::pixels() const {
    return pixels_.begin();
}

PPU::PPU(InterruptController& interrupts) : interrupts(interrupts) {
    reset();
}

void PPU::reset() {
    framebuffer = Framebuffer{};

    vram.fill(0);
    oam.fill(0);

    lcdc = 0;
    stat = 0;
    scx = 0;
    scy = 0;
    ly = 0;
    lyc = 0;
    wy = 0;
    wx = 0;
    bgp = 0;
    obp0 = 0;
    obp1 = 0;

    frame_ready = false;
    dot_counter = 0;
    mode3_dot_target = 172;
    scx_discard_dots = 0;
    object_penalty_dots = 0;
    screen_x = 0;
    window_line = 0;
    window_triggered_this_line = false;
    oam_dma_active = false;

    mode = Mode::HBlank;

    fetcher = PixelFetcher{};
    bg_fifo = PixelFifo{};
    obj_fifo = PixelFifo{};
    line_objects.fill(ObjectCandidate{});
    line_object_count = 0;

    stat_interrupt_line = false;
}

const Framebuffer& PPU::get_framebuffer() const {
    return framebuffer;
}

bool PPU::is_frame_ready() const {
    return frame_ready;
}

void PPU::clear_frame_ready() {
    frame_ready = false;
}

bool PPU::lcd_enabled() const {
    return (lcdc & 0x80) != 0;
}

bool PPU::vram_accessible() const {
    return !lcd_enabled() || mode != Mode::Drawing;
}

bool PPU::oam_accessible() const {
    return !lcd_enabled() || mode == Mode::HBlank || mode == Mode::VBlank;
}

uint8_t PPU::stat_value() const {
    const uint8_t mode_bits = lcd_enabled() ? static_cast<uint8_t>(mode) : 0;
    const uint8_t coincidence_flag = ly == lyc ? 0x04 : 0;

    return 0x80 | (stat & 0x78) | coincidence_flag | mode_bits;
}

uint8_t PPU::read(uint16_t addr) const {
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        return vram_accessible() ? vram[addr - 0x8000] : 0xFF;
    }

    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        return oam_accessible() ? oam[addr - 0xFE00] : 0xFF;
    }

    switch (addr) {
        case 0xFF40: return lcdc;
        case 0xFF41: return stat_value();
        case 0xFF42: return scy;
        case 0xFF43: return scx;
        case 0xFF44: return ly;
        case 0xFF45: return lyc;
        case 0xFF47: return bgp;
        case 0xFF48: return obp0;
        case 0xFF49: return obp1;
        case 0xFF4A: return wy;
        case 0xFF4B: return wx;
        default: return 0xFF;
    }
}

void PPU::write(uint16_t addr, uint8_t value) {
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        if (vram_accessible()) {
            vram[addr - 0x8000] = value;
        }
        return;
    }

    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        if (oam_accessible()) {
            oam[addr - 0xFE00] = value;
        }
        return;
    }

    switch (addr) {
        case 0xFF40: {
            const bool was_enabled = lcd_enabled();
            lcdc = value;

            if (was_enabled && !lcd_enabled()) {
                ly = 0;
                dot_counter = 0;
                screen_x = 0;
                window_line = 0;
                window_triggered_this_line = false;
                set_mode(Mode::HBlank);
                clear_fifos();
            } else if (!was_enabled && lcd_enabled()) {
                ly = 0;
                dot_counter = 0;
                window_line = 0;
                set_mode(Mode::OamScan);
                begin_scanline();
            }
            break;
        }
        case 0xFF41:
            stat = value & 0x78;
            update_stat_interrupt();
            break;
        case 0xFF42:
            scy = value;
            break;
        case 0xFF43:
            scx = value;
            break;
        case 0xFF44:
            break;
        case 0xFF45:
            lyc = value;
            update_stat_interrupt();
            break;
        case 0xFF47:
            bgp = value;
            break;
        case 0xFF48:
            obp0 = value;
            break;
        case 0xFF49:
            obp1 = value;
            break;
        case 0xFF4A:
            wy = value;
            break;
        case 0xFF4B:
            wx = value;
            break;
        default:
            break;
    }
}

void PPU::write_oam_dma(uint16_t offset, uint8_t value) {
    assert(offset < oam.size());
    oam[offset] = value;
}

void PPU::set_oam_dma_active(bool active) {
    oam_dma_active = active;
}

void PPU::update_stat_interrupt() {
    bool interrupt_line = false;

    if (lcd_enabled()) {
        const bool lyc_interrupt = (stat & 0x40) != 0 && ly == lyc;
        const bool oam_interrupt = (stat & 0x20) != 0 && mode == Mode::OamScan;
        const bool vblank_interrupt = (stat & 0x10) != 0 && mode == Mode::VBlank;
        const bool hblank_interrupt = (stat & 0x08) != 0 && mode == Mode::HBlank;

        interrupt_line = lyc_interrupt || oam_interrupt || vblank_interrupt || hblank_interrupt;
    }

    if (interrupt_line && !stat_interrupt_line) {
        interrupts.request(Interrupt::LCDStat);
    }

    stat_interrupt_line = interrupt_line;
}

void PPU::set_mode(Mode next_mode) {
    assert(lcd_enabled() || next_mode == Mode::HBlank);

    mode = next_mode;
    update_stat_interrupt();
}
