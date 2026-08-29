#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "interrupt_controller.h"
#include "ppu.h"

namespace {

constexpr uint16_t vram_start = 0x8000;
constexpr uint16_t bg_tile_map = 0x9800;
constexpr uint16_t window_tile_map = 0x9C00;
constexpr uint16_t oam_start = 0xFE00;
constexpr uint16_t lcdc = 0xFF40;
constexpr uint16_t stat = 0xFF41;
constexpr uint16_t scy = 0xFF42;
constexpr uint16_t scx = 0xFF43;
constexpr uint16_t ly = 0xFF44;
constexpr uint16_t lyc = 0xFF45;
constexpr uint16_t bgp = 0xFF47;
constexpr uint16_t obp0 = 0xFF48;
constexpr uint16_t obp1 = 0xFF49;
constexpr uint16_t wy = 0xFF4A;
constexpr uint16_t wx = 0xFF4B;

constexpr uint8_t hblank_mode = 0;
constexpr uint8_t vblank_mode = 1;
constexpr uint8_t oam_scan_mode = 2;
constexpr uint8_t drawing_mode = 3;
constexpr uint8_t vblank_interrupt = 0x01;
constexpr uint8_t stat_interrupt = 0x02;
constexpr int dots_per_line = 456;
constexpr int lcd_enable_line_dots = 455;
constexpr int first_frame_dots =
    lcd_enable_line_dots + dots_per_line * 153;

uint8_t current_mode(const PPU& ppu)
{
    return ppu.read(stat) & 0x03;
}

void write_solid_tile(PPU& ppu, uint8_t tile, uint8_t color)
{
    const uint16_t tile_address = vram_start + static_cast<uint16_t>(tile) * 16;
    const uint8_t low = (color & 0x01) != 0 ? 0xFF : 0x00;
    const uint8_t high = (color & 0x02) != 0 ? 0xFF : 0x00;

    for (uint16_t row = 0; row < 8; ++row) {
        ppu.write(tile_address + row * 2, low);
        ppu.write(tile_address + row * 2 + 1, high);
    }
}

void clear_oam(PPU& ppu)
{
    for (uint16_t offset = 0; offset < 160; ++offset) {
        ppu.write(oam_start + offset, 0x00);
    }
}

void write_oam_word(PPU& ppu, uint16_t offset, uint16_t value)
{
    ppu.write(oam_start + offset, value & 0xFF);
    ppu.write(oam_start + offset + 1, value >> 8);
}

uint16_t read_oam_word(const PPU& ppu, uint16_t offset)
{
    return static_cast<uint16_t>(
        ppu.read(oam_start + offset) |
        static_cast<uint16_t>(ppu.read(oam_start + offset + 1)) << 8
    );
}

void tick_to_second_frame(PPU& ppu)
{
    ppu.tick_dots(first_frame_dots);
}

void tick_to_first_normal_line(PPU& ppu)
{
    ppu.tick_dots(lcd_enable_line_dots);
}

} // namespace

TEST_CASE("Framebuffer stores pixels in row-major order")
{
    Framebuffer framebuffer;

    framebuffer.set_pixel(0, 0, 0x11223344);
    framebuffer.set_pixel(17, 23, 0x55667788);
    framebuffer.set_pixel(Framebuffer::width - 1, Framebuffer::height - 1, 0x99AABBCC);

    const uint32_t* pixels = framebuffer.pixels();

    REQUIRE(pixels[0] == 0x11223344);
    REQUIRE(pixels[23 * Framebuffer::width + 17] == 0x55667788);
    REQUIRE(pixels[Framebuffer::width * Framebuffer::height - 1] == 0x99AABBCC);
}

TEST_CASE("PPU reset disables the LCD and clears timing state")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    REQUIRE(ppu.read(lcdc) == 0x00);
    REQUIRE(ppu.read(ly) == 0x00);
    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE_FALSE(ppu.is_frame_ready());
}

TEST_CASE("PPU registers are readable and writable")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    ppu.write(lcdc, 0x7F);
    ppu.write(scy, 0x12);
    ppu.write(scx, 0x23);
    ppu.write(lyc, 0x34);
    ppu.write(bgp, 0x45);
    ppu.write(obp0, 0x56);
    ppu.write(obp1, 0x67);
    ppu.write(wy, 0x78);
    ppu.write(wx, 0x89);

    REQUIRE(ppu.read(lcdc) == 0x7F);
    REQUIRE(ppu.read(scy) == 0x12);
    REQUIRE(ppu.read(scx) == 0x23);
    REQUIRE(ppu.read(lyc) == 0x34);
    REQUIRE(ppu.read(bgp) == 0x45);
    REQUIRE(ppu.read(obp0) == 0x56);
    REQUIRE(ppu.read(obp1) == 0x67);
    REQUIRE(ppu.read(wy) == 0x78);
    REQUIRE(ppu.read(wx) == 0x89);
}

TEST_CASE("PPU STAT mode and coincidence bits are read-only")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    const uint8_t status_bits = ppu.read(stat) & 0x07;

    ppu.write(stat, 0xFF);

    REQUIRE((ppu.read(stat) & 0x78) == 0x78);
    REQUIRE((ppu.read(stat) & 0x07) == status_bits);

    ppu.write(stat, 0x00);

    REQUIRE((ppu.read(stat) & 0x78) == 0x00);
    REQUIRE((ppu.read(stat) & 0x07) == status_bits);
}

TEST_CASE("PPU LY is read-only")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    ppu.tick_dots(lcd_enable_line_dots);
    REQUIRE(ppu.read(ly) == 1);

    ppu.write(ly, 0x72);

    REQUIRE(ppu.read(ly) == 1);
}

TEST_CASE("PPU returns open bus for addresses outside its mapped registers and memory")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    REQUIRE(ppu.read(0x7FFF) == 0xFF);
    REQUIRE(ppu.read(0xA000) == 0xFF);
    REQUIRE(ppu.read(0xFEA0) == 0xFF);
    REQUIRE(ppu.read(0xFF46) == 0xFF);
}

TEST_CASE("PPU exposes VRAM and OAM while the LCD is disabled")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    ppu.write(0x8000, 0x12);
    ppu.write(0x9FFF, 0x34);
    ppu.write(0xFE00, 0x56);
    ppu.write(0xFE9F, 0x78);

    REQUIRE(ppu.read(0x8000) == 0x12);
    REQUIRE(ppu.read(0x9FFF) == 0x34);
    REQUIRE(ppu.read(0xFE00) == 0x56);
    REQUIRE(ppu.read(0xFE9F) == 0x78);
}

TEST_CASE("PPU enabling the DMG LCD starts line zero in mode 0")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    ppu.write(lcdc, 0x80);

    REQUIRE(ppu.read(ly) == 0);
    REQUIRE(current_mode(ppu) == hblank_mode);

    ppu.tick_dots(78);
    REQUIRE(current_mode(ppu) == hblank_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == drawing_mode);
}

TEST_CASE("PPU applies the DMG LCD-enable access phases")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(0x8000, 0x11);
    ppu.write(0xFE00, 0x22);
    ppu.write(lcdc, 0x80);

    ppu.tick_dots(76);
    ppu.write(0xFE00, 0x33);
    REQUIRE(ppu.read(0xFE00) == 0x33);

    ppu.tick_dots(1);
    ppu.write(0x8000, 0x44);
    ppu.write(0xFE00, 0x55);
    REQUIRE(ppu.read(0x8000) == 0x44);
    REQUIRE(ppu.read(0xFE00) == 0x33);

    ppu.tick_dots(2);
    REQUIRE(current_mode(ppu) == drawing_mode);
    REQUIRE(ppu.read(0x8000) == 0xFF);
    REQUIRE(ppu.read(0xFE00) == 0xFF);

    ppu.tick_dots(172);
    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE(ppu.read(0x8000) == 0x44);
    REQUIRE(ppu.read(0xFE00) == 0x33);

    ppu.tick_dots(203);
    REQUIRE(ppu.read(ly) == 0);

    ppu.tick_dots(1);
    REQUIRE(ppu.read(ly) == 1);
    REQUIRE(current_mode(ppu) == oam_scan_mode);
}

TEST_CASE("PPU visible scanline follows mode 2, mode 3, and mode 0 timing")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);

    ppu.tick_dots(79);
    REQUIRE(current_mode(ppu) == oam_scan_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(171);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);

    ppu.tick_dots(203);
    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE(ppu.read(ly) == 1);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == oam_scan_mode);
    REQUIRE(ppu.read(ly) == 2);
}

TEST_CASE("PPU tick carries partial scanline dots between calls")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);

    ppu.tick_dots(200);
    ppu.tick_dots(254);
    REQUIRE(ppu.read(ly) == 0);

    ppu.tick_dots(1);
    REQUIRE(ppu.read(ly) == 1);
    REQUIRE(current_mode(ppu) == oam_scan_mode);
}

TEST_CASE("PPU disables CPU OAM access in modes 2 and 3 and VRAM access in mode 3")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(0x8000, 0x11);
    ppu.write(0xFE00, 0x22);
    ppu.write(lcdc, 0x80);

    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE(ppu.read(0x8000) == 0x11);
    REQUIRE(ppu.read(0xFE00) == 0x22);

    tick_to_first_normal_line(ppu);

    REQUIRE(current_mode(ppu) == oam_scan_mode);
    REQUIRE(ppu.read(0x8000) == 0x11);
    REQUIRE(ppu.read(0xFE00) == 0xFF);

    ppu.write(0x8000, 0x33);
    ppu.write(0xFE00, 0x44);
    ppu.tick_dots(80);

    REQUIRE(current_mode(ppu) == drawing_mode);
    REQUIRE(ppu.read(0x8000) == 0xFF);
    REQUIRE(ppu.read(0xFE00) == 0xFF);

    ppu.write(0x8000, 0x55);
    ppu.write(0xFE00, 0x66);
    ppu.tick_dots(172);

    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE(ppu.read(0x8000) == 0x33);
    REQUIRE(ppu.read(0xFE00) == 0x22);

    ppu.write(0x8000, 0x77);
    ppu.write(0xFE00, 0x88);

    REQUIRE(ppu.read(0x8000) == 0x77);
    REQUIRE(ppu.read(0xFE00) == 0x88);
}

TEST_CASE("PPU DMA writes bypass CPU OAM access restrictions")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);

    ppu.write_oam_dma(0, 0x12);
    ppu.write_oam_dma(159, 0x34);
    REQUIRE(ppu.read(0xFE00) == 0xFF);

    ppu.tick_dots(252);

    REQUIRE(ppu.read(0xFE00) == 0x12);
    REQUIRE(ppu.read(0xFE9F) == 0x34);
}

TEST_CASE("PPU enters VBlank after 144 visible scanlines")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);

    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 143 - 1);
    REQUIRE(ppu.read(ly) == 143);
    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE_FALSE(ppu.is_frame_ready());
    REQUIRE((interrupts.read_if() & vblank_interrupt) == 0);

    ppu.tick_dots(1);

    REQUIRE(ppu.read(ly) == 144);
    REQUIRE(current_mode(ppu) == vblank_mode);
    REQUIRE(ppu.is_frame_ready());
    REQUIRE((interrupts.read_if() & vblank_interrupt) != 0);
}

TEST_CASE("PPU VBlank lasts ten scanlines before the next frame")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 143);

    ppu.tick_dots(dots_per_line * 9 + 5);
    REQUIRE(ppu.read(ly) == 153);
    REQUIRE(current_mode(ppu) == vblank_mode);

    ppu.tick_dots(1);

    REQUIRE(ppu.read(ly) == 0);
    REQUIRE(current_mode(ppu) == vblank_mode);

    ppu.tick_dots(450);

    REQUIRE(ppu.read(ly) == 0);
    REQUIRE(current_mode(ppu) == oam_scan_mode);
}

TEST_CASE("PPU frame-ready flag remains set until cleared")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 143);

    REQUIRE(ppu.is_frame_ready());

    ppu.tick_dots(456);
    REQUIRE(ppu.is_frame_ready());

    ppu.clear_frame_ready();
    REQUIRE_FALSE(ppu.is_frame_ready());
}

TEST_CASE("PPU disabling the LCD resets LY and stops scanline timing")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 6 + 100);
    REQUIRE(ppu.read(ly) == 7);

    ppu.write(lcdc, 0x00);

    REQUIRE(ppu.read(ly) == 0);
    REQUIRE(current_mode(ppu) == hblank_mode);

    ppu.tick_dots(456 * 3);
    REQUIRE(ppu.read(ly) == 0);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU updates the STAT coincidence flag when LY equals LYC")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();

    ppu.write(lyc, 1);
    REQUIRE((ppu.read(stat) & 0x04) == 0);

    ppu.write(lyc, 0);
    REQUIRE((ppu.read(stat) & 0x04) != 0);

    ppu.write(lcdc, 0x80);
    ppu.tick_dots(lcd_enable_line_dots);
    REQUIRE(ppu.read(ly) == 1);
    REQUIRE((ppu.read(stat) & 0x04) == 0);
}

TEST_CASE("PPU requests a STAT interrupt when LY reaches LYC")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lyc, 1);
    ppu.write(stat, 0x40);
    ppu.write(lcdc, 0x80);
    interrupts.write_if(0x00);

    ppu.tick_dots(lcd_enable_line_dots - 1);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(1);

    REQUIRE(ppu.read(ly) == 1);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(4);

    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);
}

TEST_CASE("PPU requests a STAT interrupt on entry to an enabled mode")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(stat, 0x08);
    ppu.write(lcdc, 0x80);
    interrupts.write_if(0x00);

    ppu.tick_dots(250);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(1);

    REQUIRE(current_mode(ppu) == hblank_mode);
    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);
}

TEST_CASE("PPU STAT interrupt source is edge-triggered")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(stat, 0x20);
    ppu.write(lcdc, 0x80);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(lcd_enable_line_dots);
    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);

    interrupts.write_if(0x00);
    ppu.tick_dots(1);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(dots_per_line - 1);
    REQUIRE(ppu.read(ly) == 2);
    REQUIRE(current_mode(ppu) == oam_scan_mode);
    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);
}

TEST_CASE("PPU requests VBlank and enabled mode 1 STAT interrupts together")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(stat, 0x10);
    ppu.write(lcdc, 0x80);
    interrupts.write_if(0x00);

    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 143);

    REQUIRE((interrupts.read_if() & vblank_interrupt) != 0);
    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);
}

TEST_CASE("PPU applies the DMG STAT write interrupt glitch outside mode 3")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lyc, 2);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);
    interrupts.write_if(0x00);

    ppu.write(stat, 0x00);

    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);

    interrupts.write_if(0x00);
    ppu.tick_dots(80);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.write(stat, 0x00);

    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);
}

TEST_CASE("PPU exposes the transient DMG STAT write value for one M-cycle")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);

    ppu.write(stat, 0x00);
    REQUIRE((ppu.read(stat) & 0x78) == 0x78);

    ppu.tick_dots(3);
    REQUIRE((ppu.read(stat) & 0x78) == 0x78);

    ppu.tick_dots(1);
    REQUIRE((ppu.read(stat) & 0x78) == 0x00);
}

TEST_CASE("PPU applies the DMG LY and coincidence phases on line 153")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(lyc, 0);
    ppu.write(stat, 0x40);
    ppu.write(lcdc, 0x80);
    interrupts.write_if(0x00);

    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 152 + 5);
    REQUIRE(ppu.read(ly) == 153);
    REQUIRE((ppu.read(stat) & 0x04) == 0);
    REQUIRE((interrupts.read_if() & stat_interrupt) == 0);

    ppu.tick_dots(1);

    REQUIRE(ppu.read(ly) == 0);
    REQUIRE((ppu.read(stat) & 0x04) == 0);

    ppu.tick_dots(6);

    REQUIRE((ppu.read(stat) & 0x04) != 0);
    REQUIRE((interrupts.read_if() & stat_interrupt) != 0);
}

TEST_CASE("PPU renders background tile bitplanes from most-significant bit first")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(bg_tile_map, 0x00);
    ppu.write(0x8000, 0x80);
    ppu.write(0x8001, 0x00);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x91);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] != pixels[1]);
    REQUIRE(pixels[1] == pixels[7]);
}

TEST_CASE("PPU keeps the first frame after LCD enable blank")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 1);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x91);

    ppu.tick_dots(lcd_enable_line_dots + dots_per_line * 143);
    REQUIRE(ppu.get_framebuffer().pixels()[0] == 0xFFFFFFFF);

    ppu.tick_dots(dots_per_line * 11);
    REQUIRE(ppu.get_framebuffer().pixels()[0] != 0xFFFFFFFF);
}

TEST_CASE("PPU applies SCX when selecting background tiles")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 1);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(bg_tile_map + 1, 0x01);
    ppu.write(bg_tile_map + 2, 0x00);
    ppu.write(scx, 8);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x91);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[7]);
    REQUIRE(pixels[7] != pixels[8]);
}

TEST_CASE("PPU starts the window at WX minus seven")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 3);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(bg_tile_map + 1, 0x00);
    ppu.write(window_tile_map, 0x01);
    ppu.write(window_tile_map + 1, 0x01);
    ppu.write(wy, 0);
    ppu.write(wx, 15);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0xF1);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[7]);
    REQUIRE(pixels[7] != pixels[8]);
    REQUIRE(pixels[8] == pixels[15]);
}

TEST_CASE("PPU renders the complete scanline when the window starts off screen")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 1);
    for (uint16_t tile = 0; tile < 32; ++tile) {
        ppu.write(window_tile_map + tile, 1);
    }
    ppu.write(wy, 0);
    ppu.write(wx, 1);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0xF1);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    REQUIRE(
        ppu.get_framebuffer().pixels()[Framebuffer::width - 1] !=
        0xFFFFFFFF
    );
}

TEST_CASE("PPU WX 166 advances the DMG window without drawing its normal pixels")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    ppu.write(0x8010, 0xFF);
    ppu.write(0x8011, 0x00);
    ppu.write(0x8012, 0x00);
    ppu.write(0x8013, 0xFF);
    ppu.write(0x8014, 0xFF);
    ppu.write(0x8015, 0xFF);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(window_tile_map, 0x01);
    ppu.write(wy, 0);
    ppu.write(wx, 166);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0xF1);

    tick_to_second_frame(ppu);
    ppu.tick_dots(dots_per_line);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[Framebuffer::width - 1] == 0xFFFFFFFF);

    ppu.write(wx, 7);
    ppu.tick_dots(dots_per_line);

    REQUIRE(pixels[Framebuffer::width] == 0x000000FF);
}

TEST_CASE("PPU can restart the DMG window on a later X position in the same line")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    ppu.write(0x8010, 0xFF);
    ppu.write(0x8011, 0x00);
    ppu.write(0x8012, 0x00);
    ppu.write(0x8013, 0xFF);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(window_tile_map, 0x01);
    ppu.write(wy, 0);
    ppu.write(wx, 7);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0xF1);

    tick_to_second_frame(ppu);
    ppu.tick_dots(112);
    ppu.write(lcdc, 0xD1);
    ppu.tick_dots(8);
    ppu.write(wx, 80);
    ppu.write(lcdc, 0xF1);
    ppu.tick_dots(dots_per_line - 120);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == 0xAAAAAAFF);
    REQUIRE(pixels[72] == 0xFFFFFFFF);
    REQUIRE(pixels[73] == 0x555555FF);
}

TEST_CASE("PPU renders an object at its OAM position")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 1);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(oam_start, 16);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 2, 1);
    ppu.write(oam_start + 3, 0);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x93);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[7]);
    REQUIRE(pixels[7] != pixels[8]);
}

TEST_CASE("PPU object attribute fetches lose OAM access during DMA")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    write_solid_tile(ppu, 1, 1);
    ppu.write(oam_start, 16);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 2, 1);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x93);

    tick_to_second_frame(ppu);
    ppu.tick_dots(79);
    ppu.set_oam_dma_active(true);
    ppu.tick_dots(377);
    ppu.set_oam_dma_active(false);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[8]);
}

TEST_CASE("PPU hides a low-priority object behind a nonzero background pixel")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    write_solid_tile(ppu, 0, 1);
    write_solid_tile(ppu, 1, 2);
    ppu.write(bg_tile_map, 0x00);
    ppu.write(bg_tile_map + 1, 0x00);
    ppu.write(oam_start, 16);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 2, 1);
    ppu.write(oam_start + 3, 0x80);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x93);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[8]);
}

TEST_CASE("PPU uses signed background tile addressing when LCDC bit 4 is clear")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0xFF, 1);
    ppu.write(bg_tile_map, 0xFF);
    ppu.write(bg_tile_map + 1, 0x00);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x81);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[7]);
    REQUIRE(pixels[7] != pixels[8]);
}

TEST_CASE("PPU applies SCY when selecting a background tile row")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 2);
    ppu.write(bg_tile_map + 32, 0x01);
    ppu.write(bg_tile_map + 33, 0x00);
    ppu.write(scy, 8);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x91);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[7]);
    REQUIRE(pixels[7] != pixels[8]);
}

TEST_CASE("PPU outputs background color zero when the DMG background is disabled")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(bg_tile_map, 0x00);
    ppu.write(0x8000, 0x80);
    ppu.write(0x8001, 0x80);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0x90);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[1]);
}

TEST_CASE("PPU advances the window line only after drawing the window")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(0x8010, 0xFF);
    ppu.write(0x8011, 0x00);
    ppu.write(0x8012, 0x00);
    ppu.write(0x8013, 0xFF);
    ppu.write(window_tile_map, 0x01);
    ppu.write(window_tile_map + 32, 0x01);
    ppu.write(wy, 0);
    ppu.write(wx, 7);
    ppu.write(bgp, 0xE4);
    ppu.write(lcdc, 0xF1);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456 * 2);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] != pixels[Framebuffer::width]);
}

TEST_CASE("PPU applies object X flipping")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    ppu.write(0x8010, 0x80);
    ppu.write(0x8011, 0x00);
    ppu.write(oam_start, 16);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 2, 1);
    ppu.write(oam_start + 3, 0x20);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x93);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[8]);
    REQUIRE(pixels[7] != pixels[8]);
}

TEST_CASE("PPU uses the second tile for the lower half of an 8 by 16 object")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    write_solid_tile(ppu, 0, 0);
    write_solid_tile(ppu, 1, 2);
    ppu.write(oam_start, 16);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 2, 1);
    ppu.write(oam_start + 3, 0);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x97);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456 * 9);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] != pixels[8 * Framebuffer::width]);
}

TEST_CASE("PPU limits object selection to the first ten Y-matching OAM entries")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    write_solid_tile(ppu, 1, 1);

    for (uint16_t index = 0; index < 10; ++index) {
        ppu.write(oam_start + index * 4, 16);
        ppu.write(oam_start + index * 4 + 1, 0);
    }

    ppu.write(oam_start + 40, 16);
    ppu.write(oam_start + 41, 8);
    ppu.write(oam_start + 42, 1);
    ppu.write(bgp, 0xE4);
    ppu.write(obp0, 0xE4);
    ppu.write(lcdc, 0x93);

    tick_to_second_frame(ppu);
    ppu.tick_dots(456);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == pixels[8]);
}

TEST_CASE("PPU fine scrolling extends mode 3")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(scx, 7);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);

    ppu.tick_dots(258);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU latches the low SCX bits when mode 3 begins")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(scx, 0);
    ppu.write(lcdc, 0x80);
    tick_to_second_frame(ppu);

    ppu.tick_dots(80);
    ppu.write(scx, 7);

    ppu.tick_dots(171);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU samples SCY separately for both background bitplanes")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(bg_tile_map, 0);
    ppu.write(0x8000, 0xFF);
    ppu.write(0x8001, 0x00);
    ppu.write(0x8002, 0x00);
    ppu.write(0x8003, 0xFF);
    ppu.write(bgp, 0xE4);
    ppu.write(scy, 0);
    ppu.write(lcdc, 0x91);
    tick_to_second_frame(ppu);

    ppu.tick_dots(88);
    ppu.write(scy, 1);
    ppu.tick_dots(dots_per_line - 88);

    const uint32_t* pixels = ppu.get_framebuffer().pixels();
    REQUIRE(pixels[0] == 0x000000FF);
    REQUIRE(pixels[8] == 0x555555FF);
}

TEST_CASE("PPU latches the WY condition only at the start of mode 2")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(wy, 1);
    ppu.write(wx, 7);
    ppu.write(lcdc, 0xF1);

    tick_to_first_normal_line(ppu);
    ppu.write(wy, 0);
    ppu.tick_dots(257);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU latches WY coincidence while the window is disabled")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(wy, 1);
    ppu.write(wx, 7);
    ppu.write(lcdc, 0x91);
    tick_to_first_normal_line(ppu);

    ppu.write(lcdc, 0xB1);
    ppu.tick_dots(257);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU window startup extends mode 3 by six dots")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    ppu.write(wy, 1);
    ppu.write(wx, 7);
    ppu.write(lcdc, 0xF1);
    tick_to_first_normal_line(ppu);

    ppu.tick_dots(257);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU object fetching extends mode 3")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    ppu.write(oam_start, 17);
    ppu.write(oam_start + 1, 8);
    ppu.write(lcdc, 0x82);
    tick_to_first_normal_line(ppu);

    ppu.tick_dots(262);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU charges only the flat object cost twice within one background tile")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    ppu.write(oam_start, 17);
    ppu.write(oam_start + 1, 8);
    ppu.write(oam_start + 4, 17);
    ppu.write(oam_start + 5, 12);
    ppu.write(lcdc, 0x82);
    tick_to_first_normal_line(ppu);

    ppu.tick_dots(268);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU discovers objects progressively during the mode 2 scan")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    clear_oam(ppu);
    ppu.write(lcdc, 0x82);
    tick_to_first_normal_line(ppu);
    ppu.tick_dots(2);

    ppu.write_oam_dma(4, 17);
    ppu.write_oam_dma(5, 13);
    ppu.tick_dots(255);
    REQUIRE(current_mode(ppu) == drawing_mode);

    ppu.tick_dots(1);
    REQUIRE(current_mode(ppu) == hblank_mode);
}

TEST_CASE("PPU applies the DMG OAM write corruption pattern during mode 2")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_oam_word(ppu, 8, 0x0F0F);
    write_oam_word(ppu, 10, 0x1122);
    write_oam_word(ppu, 12, 0x3333);
    write_oam_word(ppu, 14, 0x4455);
    write_oam_word(ppu, 16, 0xAAAA);
    write_oam_word(ppu, 18, 0xBBBB);
    write_oam_word(ppu, 20, 0xCCCC);
    write_oam_word(ppu, 22, 0xDDDD);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);
    ppu.tick_dots(8);

    ppu.notify_oam_bus_access(0xFE00, BusAccessType::Write);
    ppu.tick_dots(244);

    const uint16_t expected =
        static_cast<uint16_t>(((0xAAAA ^ 0x3333) & (0x0F0F ^ 0x3333)) ^ 0x3333);
    REQUIRE(read_oam_word(ppu, 16) == expected);
    REQUIRE(read_oam_word(ppu, 18) == 0x1122);
    REQUIRE(read_oam_word(ppu, 20) == 0x3333);
    REQUIRE(read_oam_word(ppu, 22) == 0x4455);
}

TEST_CASE("PPU applies the DMG OAM read corruption pattern during mode 2")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_oam_word(ppu, 8, 0x0F0F);
    write_oam_word(ppu, 12, 0x3333);
    write_oam_word(ppu, 16, 0xAAAA);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);
    ppu.tick_dots(8);

    ppu.notify_oam_bus_access(0xFE00, BusAccessType::Read);
    ppu.tick_dots(244);

    REQUIRE(read_oam_word(ppu, 16) == static_cast<uint16_t>(0x0F0F | (0xAAAA & 0x3333)));
}

TEST_CASE("PPU applies combined DMG OAM read and internal corruption")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    const uint16_t a = 0x0F0F;
    const uint16_t b = 0xAAAA;
    const uint16_t c = 0x00FF;
    const uint16_t d = 0x3333;
    const uint16_t expected = static_cast<uint16_t>(
        (b & (a | c | d)) | (a & c & d)
    );

    write_oam_word(ppu, 16, a);
    write_oam_word(ppu, 24, b);
    write_oam_word(ppu, 26, 0x1111);
    write_oam_word(ppu, 28, d);
    write_oam_word(ppu, 30, 0x4444);
    write_oam_word(ppu, 32, c);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);
    ppu.tick_dots(16);

    ppu.notify_oam_bus_access(0xFE00, BusAccessType::ReadAndInternal);
    ppu.write(lcdc, 0x00);

    REQUIRE(read_oam_word(ppu, 16) == expected);
    REQUIRE(read_oam_word(ppu, 18) == 0x1111);
    REQUIRE(read_oam_word(ppu, 20) == d);
    REQUIRE(read_oam_word(ppu, 22) == 0x4444);
    REQUIRE(read_oam_word(ppu, 24) == expected);
    REQUIRE(read_oam_word(ppu, 32) == expected);
    REQUIRE(read_oam_word(ppu, 34) == 0x1111);
    REQUIRE(read_oam_word(ppu, 36) == d);
    REQUIRE(read_oam_word(ppu, 38) == 0x4444);
}

TEST_CASE("PPU suppresses CPU OAM corruption while OAM DMA is active")
{
    InterruptController interrupts;
    PPU ppu(interrupts);

    interrupts.reset();
    ppu.reset();
    write_oam_word(ppu, 8, 0x0F0F);
    write_oam_word(ppu, 12, 0x3333);
    write_oam_word(ppu, 16, 0xAAAA);
    ppu.write(lcdc, 0x80);
    tick_to_first_normal_line(ppu);
    ppu.tick_dots(8);
    ppu.set_oam_dma_active(true);

    ppu.notify_oam_bus_access(0xFE00, BusAccessType::Write);
    ppu.set_oam_dma_active(false);
    ppu.tick_dots(244);

    REQUIRE(read_oam_word(ppu, 16) == 0xAAAA);
}
