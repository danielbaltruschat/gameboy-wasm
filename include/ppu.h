#pragma once

#include <array>
#include <cstdint>

#include "dmg_clock.h"
#include "bus.h"

class InterruptController;

class Framebuffer {
public:
    static constexpr int width = 160;
    static constexpr int height = 144;

    void set_pixel(int x, int y, uint32_t rgba);
    const uint32_t* pixels() const;

private:
    std::array<uint32_t, width * height> pixels_;
};

class PPU {
public:
    explicit PPU(InterruptController& interrupts);

    void reset();
    void tick_dots(dmg::DotCount dots);

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    void write_oam_dma(uint16_t offset, uint8_t value);
    void set_oam_dma_active(bool active);
    void notify_oam_bus_access(uint16_t addr, BusAccessType access_type);

    const Framebuffer& get_framebuffer() const;
    bool is_frame_ready() const;
    void clear_frame_ready();

private:
    enum class Mode : uint8_t {
        HBlank = 0,
        VBlank = 1,
        OamScan = 2,
        Drawing = 3,
    };

    struct PixelFetcher {
        uint8_t x = 0;
        uint8_t y = 0;
        uint8_t tile_id = 0;
        uint8_t tile_data_low = 0;
        uint8_t tile_data_high = 0;
        uint8_t step = 0;
        bool fetching_window = false;
    };

    struct FifoPixel {
        uint8_t color = 0;
        uint8_t palette = 0;
        bool obj_to_bg_priority = false;
        uint8_t oam_index = 0xFF;
    };

    struct PixelFifo {
        std::array<FifoPixel, 16> pixels{};
        uint8_t head = 0;
        uint8_t size = 0;
    };

    struct ObjectCandidate {
        uint8_t y = 0;
        uint8_t x = 0;
        uint8_t tile = 0;
        uint8_t attributes = 0;
        uint8_t oam_index = 0;
    };

    InterruptController& interrupts;
    Framebuffer framebuffer;

    // PPU memory/state
    std::array<uint8_t, 0x2000> vram;
    std::array<uint8_t, 160> oam;

    uint8_t lcdc;
    uint8_t stat;
    uint8_t scx;
    uint8_t scy;
    uint8_t ly;
    uint8_t lyc;
    uint8_t wy;
    uint8_t wx;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;

    bool frame_ready = false;
    int dot_counter = 0;
    int mode3_dot_target = 172;
    int scx_discard_dots = 0;
    int object_penalty_dots = 0;
    uint8_t screen_x = 0;
    uint8_t window_line = 0;
    bool window_triggered_this_line = false;
    bool oam_dma_active = false;
    Mode mode = Mode::OamScan;
    PixelFetcher fetcher;
    PixelFifo bg_fifo;
    PixelFifo obj_fifo;
    std::array<ObjectCandidate, 10> line_objects{};
    uint8_t line_object_count = 0;
    bool stat_interrupt_line = false;

    bool lcd_enabled() const;
    bool vram_accessible() const;
    bool oam_accessible() const;
    uint8_t stat_value() const;
    void set_mode(Mode next_mode);
    void update_lyc();
    void update_stat_interrupt();
    void begin_scanline();
    void evaluate_objects_for_line();
    void tick_pixel_fetcher();
    void clear_fifos();
    void push_bg_pixels();
    void fetch_object_pixels(const ObjectCandidate& object);
    void mix_and_push_pixel();
    int calculate_mode3_dot_target() const;
    void corrupt_oam(BusAccessType access_type);
};
