#include "ppu.h"
#include "interrupt_controller.h"
#include <cstdint>
#include <sys/types.h>

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
