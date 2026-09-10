#pragma once

#include <array>
#include <cstdint>

#include "bus.h"

class InterruptController;

class Framebuffer {
public:
    static constexpr int width = 160;
    static constexpr int height = 144;

    Framebuffer();

    void set_pixel(int x, int y, uint32_t rgba);
    const uint32_t* pixels() const;

private:
    std::array<uint32_t, width * height> pixels_;
};

class PPU {
public:
    explicit PPU(InterruptController& interrupts);

    void reset();
    void tick_dots(int dots);

    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    void write_cpu_stat(uint8_t value);
    uint8_t read_vram_dma(uint16_t addr) const;

    void write_oam_dma(uint16_t offset, uint8_t value);
    void write_oam_dma_conflict(uint8_t value);
    void set_oam_dma_active(bool active);
    void set_oam_dma_active(bool active, uint16_t offset);
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
        uint8_t tile_id = 0;
        uint8_t tile_data_low = 0;
        uint8_t tile_data_high = 0;
        uint16_t tile_data_address = 0;
        uint8_t step = 0;
        bool fetching_window = false;
    };

    struct FifoPixel {
        uint8_t color = 0;
        uint8_t palette = 0;
        bool obj_to_bg_priority = false;
        uint8_t object_x = 0xFF;
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
        uint8_t oam_index = 0;
    };

    enum class ObjectFetchStep : uint8_t {
        None,
        Align,
        OamFirst,
        OamSecond,
        DataLowFirst,
        DataLowSecond,
        DataHighFirst,
        DataHighSecond,
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
    int line_dot_limit = 456;
    int mode3_startup_dots = 0;
    int pixel_position = -16;
    int object_fetch_index = -1;
    uint8_t screen_x = 0;
    uint8_t scanline = 0;
    uint8_t oam_scan_index = 0;
    uint8_t window_line = 0;
    uint8_t scx_low = 0;
    int ly_for_comparison = 0;
    bool window_y_triggered = false;
    bool window_triggered_this_line = false;
    bool window_active = false;
    bool wx_just_changed = false;
    bool insert_bg_pixel = false;
    bool coincidence_flag = true;
    bool first_frame_blank = true;
    bool startup_line = false;
    bool oam_dma_active = false;
    bool object_fetch_aborted = false;
    bool object_fetch_started_this_line = false;
    int oam_mode_interrupt_delay = 0;
    bool oam_mode_interrupt_active = false;
    int hblank_mode_interrupt_delay = 0;
    bool hblank_mode_interrupt_active = false;
    bool vram_read_blocked = false;
    bool vram_write_blocked = false;
    bool oam_read_blocked = false;
    bool oam_write_blocked = false;
    Mode mode = Mode::OamScan;
    Mode stat_mode = Mode::HBlank;
    Mode pending_stat_mode = Mode::HBlank;
    PixelFetcher fetcher;
    PixelFifo bg_fifo;
    PixelFifo obj_fifo;
    std::array<ObjectCandidate, 10> line_objects{};
    std::array<bool, 10> line_object_fetched{};
    uint8_t line_object_count = 0;
    ObjectFetchStep object_fetch_step = ObjectFetchStep::None;
    uint8_t object_tile = 0;
    uint8_t object_attributes = 0;
    uint8_t object_tile_data_low = 0;
    uint8_t object_tile_data_high = 0;
    uint16_t object_tile_address = 0;
    bool stat_interrupt_line = false;
    uint8_t pending_stat = 0;
    int stat_write_dots_remaining = 0;
    int stat_mode_delay = 0;
    int oam_dma_offset = 0;
    int accessed_oam_row = -1;

    bool lcd_enabled() const;
    bool vram_accessible() const;
    bool oam_accessible() const;
    bool window_enabled() const;
    uint8_t stat_value() const;
    void tick_dot();
    void update_stat_write();
    void set_access_blocking(bool vram_blocked, bool oam_blocked);
    void set_mode(Mode next_mode);
    bool stat_interrupt_active(uint8_t interrupt_selects) const;
    void update_stat_interrupt();
    void set_ly_for_comparison(int value, bool update_interrupt = true);
    void begin_scanline();
    void scan_oam_entry();
    void begin_mode3();
    void tick_mode3();
    void advance_pixel_fetcher();
    void clear_fifos();
    void push_blank_bg_pixels();
    void push_bg_pixels();
    bool try_push_bg_pixels();
    void overlay_object_pixels(const ObjectCandidate& object);
    void trigger_window();
    void start_object_fetch();
    bool object_fetch_remaining() const;
    bool tick_object_fetch();
    void mix_and_push_pixel();
    void finish_drawing_if_complete();
    uint8_t read_oam_ppu(uint16_t offset) const;
    void corrupt_oam(BusAccessType access_type);
};
