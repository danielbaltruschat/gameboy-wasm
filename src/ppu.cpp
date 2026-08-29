#include "ppu.h"
#include "interrupt_controller.h"
#include <cstdint>
#include <cassert>

namespace {

constexpr std::array<uint32_t, 4> dmg_colors = {
    0xFFFFFFFF,
    0xAAAAAAFF,
    0x555555FF,
    0x000000FF,
};

uint32_t apply_palette(uint8_t color, uint8_t palette) {
    assert(color < 4);
    const uint8_t shade = (palette >> (color * 2)) & 0x03;
    return dmg_colors[shade];
}

}

Framebuffer::Framebuffer() {
    pixels_.fill(dmg_colors[0]);
}

void Framebuffer::set_pixel(int x, int y, uint32_t rgba) {
    assert(x >= 0 && x < width);
    assert(y >= 0 && y < height);
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
    line_dot_limit = 456;
    mode3_startup_dots = 0;
    pixel_position = -16;
    object_fetch_index = -1;
    object_fetch_alignment_dots = 0;
    screen_x = 0;
    scanline = 0;
    oam_scan_index = 0;
    window_line = 0;
    scx_low = 0;
    ly_for_comparison = 0;
    window_y_triggered = false;
    window_triggered_this_line = false;
    window_active = false;
    wx_just_changed = false;
    insert_bg_pixel = false;
    coincidence_flag = true;
    first_frame_blank = true;
    startup_line = false;
    oam_dma_active = false;
    object_fetch_aborted = false;
    vram_read_blocked = false;
    vram_write_blocked = false;
    oam_read_blocked = false;
    oam_write_blocked = false;

    mode = Mode::HBlank;

    fetcher = PixelFetcher{};
    bg_fifo = PixelFifo{};
    obj_fifo = PixelFifo{};
    line_objects.fill(ObjectCandidate{});
    line_object_fetched.fill(false);
    considered_object_tiles.fill(0);
    line_object_count = 0;
    considered_object_tile_count = 0;
    object_fetch_step = ObjectFetchStep::None;
    object_tile = 0;
    object_attributes = 0;
    object_tile_data_low = 0;
    object_tile_data_high = 0;
    object_tile_address = 0;

    stat_interrupt_line = false;
    pending_stat = 0;
    stat_write_dots_remaining = 0;
    oam_dma_offset = 0;
    accessed_oam_row = -1;
}

void PPU::tick_dots(int dots) {
    assert(dots >= 0);
    assert(dot_counter >= 0 && dot_counter < line_dot_limit);
    assert(scanline <= 153);
    assert(ly <= 153);

    if (!lcd_enabled() || dots == 0) {
        return;
    }

    for (int dot = 0; dot < dots; ++dot) {
        tick_dot();
    }
}

void PPU::tick_dot() {
    update_stat_write();
    dot_counter++;

    if (scanline == 153) {
        if (dot_counter == 6) {
            ly = 0;
            set_ly_for_comparison(153);
        } else if (dot_counter == 8) {
            set_ly_for_comparison(-1);
        } else if (dot_counter == 12) {
            set_ly_for_comparison(0);
        }
    } else if (dot_counter == 4 && ly_for_comparison < 0) {
        set_ly_for_comparison(scanline);
    }

    if (startup_line && mode == Mode::HBlank) {
        if (dot_counter == 77) {
            oam_write_blocked = true;
        } else if (dot_counter == 79) {
            set_mode(Mode::Drawing);
            set_access_blocking(true, true);
            begin_mode3();
        }
    } else if (mode == Mode::OamScan) {
        if ((dot_counter & 0x01) == 0) {
            scan_oam_entry();
        }

        if (dot_counter == 80) {
            set_mode(Mode::Drawing);
            set_access_blocking(true, true);
            begin_mode3();
        }
    } else if (mode == Mode::Drawing) {
        tick_mode3();
    }

    if (dot_counter < line_dot_limit) {
        return;
    }

    dot_counter = 0;
    line_dot_limit = 456;
    startup_line = false;
    scanline = static_cast<uint8_t>((scanline + 1) % 154);
    ly = scanline;
    set_ly_for_comparison(-1);

    if (scanline == 0) {
        window_line = 0;
        window_y_triggered = false;
        set_mode(Mode::OamScan);
        set_access_blocking(false, true);
        begin_scanline();
    } else if (scanline == 144) {
        set_mode(Mode::VBlank);
        set_access_blocking(false, false);
        interrupts.request(Interrupt::VBlank);
        frame_ready = true;
        first_frame_blank = false;
    } else if (scanline < 144) {
        set_mode(Mode::OamScan);
        set_access_blocking(false, true);
        begin_scanline();
    } else {
        set_mode(Mode::VBlank);
        set_access_blocking(false, false);
    }
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
    return !lcd_enabled() || !vram_read_blocked;
}

bool PPU::oam_accessible() const {
    return !lcd_enabled() || !oam_read_blocked;
}

bool PPU::window_enabled() const {
    return
        window_y_triggered &&
        (lcdc & 0x21) == 0x21 &&
        wx <= 166;
}

uint8_t PPU::stat_value() const {
    const uint8_t mode_bits = lcd_enabled() ? static_cast<uint8_t>(mode) : 0;
    const uint8_t coincidence_bit = coincidence_flag ? 0x04 : 0;

    return 0x80 | (stat & 0x78) | coincidence_bit | mode_bits;
}

uint8_t PPU::read(uint16_t addr) const {
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        return vram_accessible() ? vram[addr - 0x8000] : 0xFF;
    }

    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        return oam_accessible() ? oam[addr - 0xFE00] : 0xFF;
    }

    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        return oam_accessible() ? 0x00 : 0xFF;
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

uint8_t PPU::read_vram_dma(uint16_t addr) const {
    assert(addr >= 0x8000 && addr <= 0x9FFF);
    return vram[addr - 0x8000];
}

void PPU::write(uint16_t addr, uint8_t value) {
    if (addr >= 0x8000 && addr <= 0x9FFF) {
        if (!lcd_enabled() || !vram_write_blocked) {
            vram[addr - 0x8000] = value;
        }
        return;
    }

    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        if (!lcd_enabled() || !oam_write_blocked) {
            oam[addr - 0xFE00] = value;
        }
        return;
    }

    switch (addr) {
        case 0xFF40: {
            const bool was_enabled = lcd_enabled();
            const bool objects_were_enabled = (lcdc & 0x02) != 0;
            lcdc = value;

            if (
                objects_were_enabled &&
                (lcdc & 0x02) == 0 &&
                object_fetch_step != ObjectFetchStep::None
            ) {
                object_fetch_aborted = true;
            }

            if (was_enabled && !lcd_enabled()) {
                ly = 0;
                scanline = 0;
                dot_counter = 0;
                line_dot_limit = 456;
                screen_x = 0;
                window_line = 0;
                scx_low = 0;
                window_y_triggered = false;
                window_triggered_this_line = false;
                window_active = false;
                wx_just_changed = false;
                insert_bg_pixel = false;
                first_frame_blank = true;
                startup_line = false;
                stat = pending_stat;
                stat_write_dots_remaining = 0;
                framebuffer = Framebuffer{};
                set_ly_for_comparison(0);
                set_mode(Mode::HBlank);
                set_access_blocking(false, false);
                clear_fifos();
                object_fetch_step = ObjectFetchStep::None;
                object_fetch_index = -1;
                object_fetch_aborted = false;
                accessed_oam_row = -1;
            } else if (!was_enabled && lcd_enabled()) {
                ly = 0;
                scanline = 0;
                dot_counter = 0;
                line_dot_limit = 455;
                window_line = 0;
                scx_low = 0;
                window_y_triggered = false;
                window_triggered_this_line = false;
                window_active = false;
                wx_just_changed = false;
                insert_bg_pixel = false;
                first_frame_blank = true;
                startup_line = true;
                framebuffer = Framebuffer{};
                set_ly_for_comparison(0);
                set_mode(Mode::HBlank);
                set_access_blocking(false, false);
            }
            break;
        }
        case 0xFF41: {
            pending_stat = value & 0x78;
            if (lcd_enabled()) {
                stat = 0x78;
                stat_write_dots_remaining = 4;
            } else {
                stat = pending_stat;
                stat_write_dots_remaining = 0;
            }
            update_stat_interrupt();
            break;
        }
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
            set_ly_for_comparison(ly_for_comparison);
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
            wx_just_changed = true;
            break;
        default:
            break;
    }
}

void PPU::write_oam_dma(uint16_t offset, uint8_t value) {
    assert(offset < oam.size());
    oam[offset] = value;
    oam_dma_offset = offset + 1;
}

void PPU::write_oam_dma_conflict(uint8_t value) {
    assert(oam_dma_active);
    assert(oam_dma_offset > 0 && oam_dma_offset <= static_cast<int>(oam.size()));
    oam[oam_dma_offset - 1] &= value;
}

void PPU::set_oam_dma_active(bool active) {
    if (active && !oam_dma_active) {
        oam_dma_offset = 0;
    }
    oam_dma_active = active;
    if (!active) {
        oam_dma_offset = 0;
    }
}

void PPU::set_oam_dma_active(bool active, uint16_t offset) {
    assert(offset <= oam.size());
    oam_dma_active = active;
    oam_dma_offset = active ? offset : 0;
}

void PPU::notify_oam_bus_access(uint16_t addr, BusAccessType access_type) {
    assert(addr >= 0xFE00 && addr <= 0xFEFF);

    const bool read_access =
        access_type == BusAccessType::Read ||
        access_type == BusAccessType::ReadAndInternal;
    const bool blocked_access = read_access
        ? oam_read_blocked
        : oam_write_blocked;

    if (
        lcd_enabled() &&
        mode == Mode::OamScan &&
        blocked_access &&
        !oam_dma_active &&
        accessed_oam_row < static_cast<int>(oam.size())
    ) {
        corrupt_oam(access_type);
    }
}

bool PPU::stat_interrupt_active(uint8_t interrupt_selects) const {
    const bool startup_hblank =
        startup_line && mode == Mode::HBlank && dot_counter < 79;

    return
        lcd_enabled() &&
        (
            ((interrupt_selects & 0x40) != 0 && coincidence_flag) ||
            ((interrupt_selects & 0x20) != 0 && mode == Mode::OamScan) ||
            ((interrupt_selects & 0x10) != 0 && mode == Mode::VBlank) ||
            (
                (interrupt_selects & 0x08) != 0 &&
                mode == Mode::HBlank &&
                !startup_hblank
            )
        );
}

void PPU::set_ly_for_comparison(int value) {
    ly_for_comparison = value;
    coincidence_flag = value >= 0 && value == lyc;
    update_stat_interrupt();
}

void PPU::update_stat_interrupt() {
    const bool interrupt_line = stat_interrupt_active(stat);

    if (interrupt_line && !stat_interrupt_line) {
        interrupts.request(Interrupt::LCDStat);
    }

    stat_interrupt_line = interrupt_line;
}

void PPU::update_stat_write() {
    if (stat_write_dots_remaining == 0) {
        return;
    }

    stat_write_dots_remaining--;
    if (stat_write_dots_remaining == 0) {
        stat = pending_stat;
        update_stat_interrupt();
    }
}

void PPU::set_access_blocking(bool vram_blocked, bool oam_blocked) {
    vram_read_blocked = vram_blocked;
    vram_write_blocked = vram_blocked;
    oam_read_blocked = oam_blocked;
    oam_write_blocked = oam_blocked;
}

void PPU::set_mode(Mode next_mode) {
    assert(lcd_enabled() || next_mode == Mode::HBlank);

    mode = next_mode;
    update_stat_interrupt();
}

void PPU::clear_fifos() {
    bg_fifo = {};
    obj_fifo = {};
}

void PPU::begin_scanline() {
    assert(lcd_enabled());
    assert(ly < 144);
    assert(mode == Mode::OamScan);

    screen_x = 0;
    pixel_position = -16;
    mode3_startup_dots = 0;
    object_fetch_index = -1;
    object_fetch_alignment_dots = 0;
    object_fetch_step = ObjectFetchStep::None;
    object_fetch_aborted = false;
    oam_scan_index = 0;
    accessed_oam_row = 0;
    if (ly == wy) {
        window_y_triggered = true;
    }
    window_triggered_this_line = false;
    window_active = false;
    wx_just_changed = false;
    insert_bg_pixel = false;
    fetcher = {};
    clear_fifos();
    line_objects.fill(ObjectCandidate{});
    line_object_fetched.fill(false);
    considered_object_tiles.fill(0);
    line_object_count = 0;
    considered_object_tile_count = 0;
}

void PPU::scan_oam_entry() {
    assert(ly < 144);
    assert(oam_scan_index < 40);

    const int object_height = (lcdc & 0x04) != 0 ? 16 : 8;
    const uint16_t offset = static_cast<uint16_t>(oam_scan_index) * 4;
    const uint8_t object_y = read_oam_ppu(offset);
    const int object_top = static_cast<int>(object_y) - 16;

    if (
        line_object_count < line_objects.size() &&
        ly >= object_top &&
        ly < object_top + object_height
    ) {
        ObjectCandidate& object = line_objects[line_object_count++];
        object.y = object_y;
        object.x = read_oam_ppu(offset + 1);
        object.oam_index = oam_scan_index;
    }

    accessed_oam_row = (oam_scan_index & ~1) * 4 + 8;
    if (oam_scan_index == 37) {
        vram_read_blocked = true;
        oam_write_blocked = false;
    }
    oam_scan_index++;
}

void PPU::begin_mode3() {
    assert(mode == Mode::Drawing);

    mode3_startup_dots = 4;
    pixel_position = -16;
    scx_low = scx & 0x07;
    screen_x = 0;
    object_fetch_index = -1;
    object_fetch_alignment_dots = 0;
    object_fetch_step = ObjectFetchStep::None;
    object_fetch_aborted = false;
    accessed_oam_row = -1;
    fetcher = {};
    clear_fifos();
    push_blank_bg_pixels();
}

void PPU::tick_mode3() {
    assert(mode == Mode::Drawing);

    if (mode3_startup_dots > 0) {
        mode3_startup_dots--;
        return;
    }

    if (!window_active && window_enabled()) {
        bool should_trigger = false;
        bool rewind_screen_x = false;

        if (wx == 0) {
            should_trigger =
                pixel_position == -7 ||
                (pixel_position == -16 && scx_low != 0) ||
                (pixel_position >= -15 && pixel_position <= -8);
        } else if (wx < 166) {
            should_trigger = wx == static_cast<uint8_t>(pixel_position + 7);
            rewind_screen_x =
                !should_trigger &&
                !wx_just_changed &&
                wx == static_cast<uint8_t>(pixel_position + 6) &&
                screen_x > 0;
            should_trigger = should_trigger || rewind_screen_x;
        }

        if (should_trigger) {
            if (rewind_screen_x) {
                screen_x--;
            }
            trigger_window();
            if (wx == 0 && scx_low != 0) {
                wx_just_changed = false;
                return;
            }
        }
    }

    if (
        !window_triggered_this_line &&
        window_y_triggered &&
        (lcdc & 0x21) == 0x21 &&
        wx == 166 &&
        pixel_position == 159
    ) {
        window_triggered_this_line = true;
        window_line++;
    }

    wx_just_changed = false;

    if (tick_object_fetch()) {
        return;
    }

    start_object_fetch();
    if (tick_object_fetch()) {
        return;
    }

    mix_and_push_pixel();
    advance_pixel_fetcher();

    finish_drawing_if_complete();
}

void PPU::advance_pixel_fetcher() {
    assert(mode == Mode::Drawing);

    switch (fetcher.step) {
    case 0: {
        if (fetcher.fetching_window && !window_enabled()) {
            fetcher.fetching_window = false;
            window_active = false;
        }

        const uint16_t map_base = fetcher.fetching_window
            ? ((lcdc & 0x40) != 0 ? 0x1C00 : 0x1800)
            : ((lcdc & 0x08) != 0 ? 0x1C00 : 0x1800);
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        uint8_t tile_x = 0;

        if (fetcher.fetching_window) {
            tile_x = fetcher.x & 0x1F;
        } else if (pixel_position < -8) {
            tile_x = scx >> 3;
        } else {
            tile_x = static_cast<uint8_t>((scx + pixel_position + 8) / 8) & 0x1F;
        }

        const uint8_t tile_y = (pixel_y / 8) & 0x1F;
        fetcher.tile_data_address = map_base + tile_y * 32 + tile_x;
        fetcher.step = 1;
        break;
    }
    case 1:
        fetcher.tile_id = vram[fetcher.tile_data_address];
        fetcher.step = 2;
        break;
    case 2: {
        const int tile_base = (lcdc & 0x10) != 0
            ? static_cast<int>(fetcher.tile_id) * 16
            : 0x1000 + static_cast<int8_t>(fetcher.tile_id) * 16;
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        fetcher.tile_data_address = static_cast<uint16_t>(
            tile_base + (pixel_y & 0x07) * 2
        );
        fetcher.step = 3;
        break;
    }
    case 3:
        fetcher.tile_data_low = vram[fetcher.tile_data_address];
        fetcher.step = 4;
        break;
    case 4: {
        const int tile_base = (lcdc & 0x10) != 0
            ? static_cast<int>(fetcher.tile_id) * 16
            : 0x1000 + static_cast<int8_t>(fetcher.tile_id) * 16;
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        fetcher.tile_data_address = static_cast<uint16_t>(
            tile_base + (pixel_y & 0x07) * 2 + 1
        );
        fetcher.step = 5;
        break;
    }
    case 5:
        fetcher.tile_data_high = vram[fetcher.tile_data_address];
        if (fetcher.fetching_window) {
            fetcher.x = static_cast<uint8_t>((fetcher.x + 1) & 0x1F);
        }
        fetcher.step = 6;
        try_push_bg_pixels();
        break;
    case 6:
        try_push_bg_pixels();
        break;
    default:
        assert(false);
        break;
    }
}

void PPU::push_bg_pixels() {
    assert(bg_fifo.size == 0);

    for (uint8_t pixel = 0; pixel < 8; ++pixel) {
        const uint8_t bit = 7 - pixel;
        const uint8_t color =
            ((fetcher.tile_data_high >> bit) & 0x01) << 1 |
            ((fetcher.tile_data_low >> bit) & 0x01);
        const uint8_t tail = (bg_fifo.head + bg_fifo.size) % bg_fifo.pixels.size();

        bg_fifo.pixels[tail] = FifoPixel{color};
        bg_fifo.size++;
    }
}

bool PPU::try_push_bg_pixels() {
    if (bg_fifo.size != 0) {
        return false;
    }

    if (window_y_triggered && (lcdc & 0x20) == 0) {
        int logical_position = pixel_position + 7;
        if (logical_position < 0 || logical_position > 167) {
            logical_position = 0;
        }
        if (wx == logical_position) {
            bg_fifo.pixels[bg_fifo.head] = {};
            bg_fifo.size = 1;
            return false;
        }
    }

    push_bg_pixels();
    fetcher.step = 0;
    return true;
}

void PPU::push_blank_bg_pixels() {
    assert(bg_fifo.size == 0);

    for (int pixel = 0; pixel < 8; ++pixel) {
        const uint8_t tail = (bg_fifo.head + bg_fifo.size) % bg_fifo.pixels.size();
        bg_fifo.pixels[tail] = {};
        bg_fifo.size++;
    }
}

void PPU::overlay_object_pixels(const ObjectCandidate& object) {
    while (obj_fifo.size < obj_fifo.pixels.size()) {
        const uint8_t tail = (obj_fifo.head + obj_fifo.size) % obj_fifo.pixels.size();
        obj_fifo.pixels[tail] = {};
        obj_fifo.size++;
    }

    const int object_left = static_cast<int>(object.x) - 8;

    for (int pixel = 0; pixel < 8; ++pixel) {
        const int screen_position = object_left + pixel;
        const int fifo_offset = screen_position - pixel_position;

        if (fifo_offset < 0 || fifo_offset >= static_cast<int>(obj_fifo.pixels.size())) {
            continue;
        }

        const uint8_t bit = (object_attributes & 0x20) != 0
            ? static_cast<uint8_t>(pixel)
            : static_cast<uint8_t>(7 - pixel);
        const uint8_t color =
            ((object_tile_data_low >> bit) & 0x01) |
            (((object_tile_data_high >> bit) & 0x01) << 1);

        if (color == 0) {
            continue;
        }

        const uint8_t fifo_index =
            (obj_fifo.head + fifo_offset) % obj_fifo.pixels.size();
        FifoPixel& existing = obj_fifo.pixels[fifo_index];

        if (existing.color != 0) {
            const bool existing_has_priority =
                existing.object_x < object.x ||
                (
                    existing.object_x == object.x &&
                    existing.oam_index < object.oam_index
                );

            if (existing_has_priority) {
                continue;
            }
        }

        existing.color = color;
        existing.palette = (object_attributes >> 4) & 0x01;
        existing.obj_to_bg_priority = (object_attributes & 0x80) != 0;
        existing.object_x = object.x;
        existing.oam_index = object.oam_index;
    }
}

void PPU::trigger_window() {
    if (window_triggered_this_line) {
        window_line++;
    }
    window_triggered_this_line = true;
    window_active = true;
    bg_fifo = {};
    fetcher = {};
    fetcher.fetching_window = true;
}

void PPU::start_object_fetch() {
    if (
        object_fetch_step != ObjectFetchStep::None ||
        (lcdc & 0x02) == 0 ||
        pixel_position >= Framebuffer::width
    ) {
        return;
    }

    const int object_match = pixel_position < -8 ? 0 : pixel_position + 8;
    int next_object = -1;

    for (uint8_t index = 0; index < line_object_count; ++index) {
        const ObjectCandidate& object = line_objects[index];
        if (line_object_fetched[index] || object.x >= 168) {
            continue;
        }

        if (object.x < object_match) {
            line_object_fetched[index] = true;
            continue;
        }

        if (object.x != object_match) {
            continue;
        }

        if (
            next_object < 0 ||
            object.oam_index < line_objects[next_object].oam_index
        ) {
            next_object = index;
        }
    }

    if (next_object >= 0) {
        const ObjectCandidate& object = line_objects[next_object];
        int alignment_dots = 5;

        if (object.x != 0) {
            const int object_left = static_cast<int>(object.x) - 8;
            const int window_left = static_cast<int>(wx) - 7;
            const bool over_window =
                window_triggered_this_line && object_left >= window_left;
            const int tile_coordinate = over_window
                ? object_left - window_left
                : object_left + scx;
            const int tile_number = tile_coordinate >= 0
                ? tile_coordinate / 8
                : (tile_coordinate - 7) / 8;
            const int tile_key = tile_number + (over_window ? 0x100 : 0);
            bool tile_was_considered = false;

            for (uint8_t index = 0; index < considered_object_tile_count; ++index) {
                if (considered_object_tiles[index] == tile_key) {
                    tile_was_considered = true;
                    break;
                }
            }

            if (tile_was_considered) {
                alignment_dots = 0;
            } else {
                int pixel_in_tile = tile_coordinate % 8;
                if (pixel_in_tile < 0) {
                    pixel_in_tile += 8;
                }
                alignment_dots = 5 - pixel_in_tile;
                if (alignment_dots < 0) {
                    alignment_dots = 0;
                }
                considered_object_tiles[considered_object_tile_count++] = tile_key;
            }
        }

        line_object_fetched[next_object] = true;
        object_fetch_index = next_object;
        object_fetch_alignment_dots = alignment_dots;
        object_fetch_step = alignment_dots == 0
            ? ObjectFetchStep::OamFirst
            : ObjectFetchStep::Align;
    }
}

bool PPU::tick_object_fetch() {
    if (object_fetch_step == ObjectFetchStep::None) {
        return false;
    }

    if (object_fetch_aborted) {
        object_fetch_step = ObjectFetchStep::None;
        object_fetch_index = -1;
        object_fetch_aborted = false;
        mix_and_push_pixel();
        advance_pixel_fetcher();
        finish_drawing_if_complete();
        return true;
    }

    assert(object_fetch_index >= 0);
    const ObjectCandidate& object = line_objects[object_fetch_index];

    switch (object_fetch_step) {
    case ObjectFetchStep::Align:
        assert(object_fetch_alignment_dots > 0);
        advance_pixel_fetcher();
        object_fetch_alignment_dots--;
        if (object_fetch_alignment_dots == 0) {
            object_fetch_step = ObjectFetchStep::OamFirst;
        }
        break;
    case ObjectFetchStep::OamFirst: {
        const uint16_t offset = static_cast<uint16_t>(object.oam_index) * 4;
        object_tile = read_oam_ppu(offset + 2);
        object_attributes = read_oam_ppu(offset + 3);
        object_fetch_step = ObjectFetchStep::OamSecond;
        break;
    }
    case ObjectFetchStep::OamSecond: {
        const int object_height = (lcdc & 0x04) != 0 ? 16 : 8;
        int row = static_cast<int>(ly) + 16 - object.y;
        if ((object_attributes & 0x40) != 0) {
            row = object_height - 1 - row;
        }

        uint8_t tile = object_tile;
        if (object_height == 16) {
            tile &= 0xFE;
            if (row >= 8) {
                tile++;
                row -= 8;
            }
        }

        object_tile_address = static_cast<uint16_t>(tile) * 16 + row * 2;
        object_fetch_step = ObjectFetchStep::DataLowFirst;
        break;
    }
    case ObjectFetchStep::DataLowFirst:
        object_tile_data_low = vram[object_tile_address];
        object_fetch_step = ObjectFetchStep::DataLowSecond;
        break;
    case ObjectFetchStep::DataLowSecond:
        object_fetch_step = ObjectFetchStep::DataHighFirst;
        break;
    case ObjectFetchStep::DataHighFirst:
        object_fetch_step = ObjectFetchStep::DataHighSecond;
        break;
    case ObjectFetchStep::DataHighSecond:
        object_tile_data_high = vram[object_tile_address + 1];
        overlay_object_pixels(object);
        object_fetch_step = ObjectFetchStep::None;
        object_fetch_index = -1;
        break;
    case ObjectFetchStep::None:
        break;
    }

    return true;
}

void PPU::finish_drawing_if_complete() {
    if (screen_x != Framebuffer::width || mode != Mode::Drawing) {
        return;
    }

    if (window_triggered_this_line) {
        window_line++;
    }
    set_mode(Mode::HBlank);
    set_access_blocking(false, false);
}

void PPU::mix_and_push_pixel() {
    if (screen_x >= Framebuffer::width || (bg_fifo.size == 0 && !insert_bg_pixel)) {
        return;
    }

    FifoPixel background_pixel{};
    if (insert_bg_pixel) {
        insert_bg_pixel = false;
    } else {
        background_pixel = bg_fifo.pixels[bg_fifo.head];
        bg_fifo.head = (bg_fifo.head + 1) % bg_fifo.pixels.size();
        bg_fifo.size--;
    }

    FifoPixel object_pixel{};
    if (obj_fifo.size > 0) {
        object_pixel = obj_fifo.pixels[obj_fifo.head];
        obj_fifo.pixels[obj_fifo.head] = {};
        obj_fifo.head = (obj_fifo.head + 1) % obj_fifo.pixels.size();
        obj_fifo.size--;
    }

    if (pixel_position < -8) {
        if ((pixel_position & 0x07) == scx_low) {
            pixel_position = -8;
        }
    }

    if (pixel_position < 0) {
        pixel_position++;
        return;
    }

    const uint8_t background_color =
        (lcdc & 0x01) != 0 ? background_pixel.color : 0;
    const bool objects_enabled = (lcdc & 0x02) != 0;

    const bool object_visible =
        objects_enabled &&
        object_pixel.color != 0 &&
        (!object_pixel.obj_to_bg_priority || background_color == 0);
    const uint8_t output_color =
        object_visible ? object_pixel.color : background_color;
    const uint8_t output_palette = object_visible
        ? (object_pixel.palette != 0 ? obp1 : obp0)
        : bgp;

    framebuffer.set_pixel(
        screen_x,
        ly,
        first_frame_blank
            ? dmg_colors[0]
            : apply_palette(output_color, output_palette)
    );
    screen_x++;
    pixel_position++;
}

uint8_t PPU::read_oam_ppu(uint16_t offset) const {
    assert(offset < oam.size());

    if (
        oam_dma_active &&
        oam_dma_offset > 0 &&
        oam_dma_offset < static_cast<int>(oam.size())
    ) {
        const int dma_word_offset = oam_dma_offset & ~1;
        return oam[dma_word_offset | (offset & 1)];
    }

    return oam[offset];
}

void PPU::corrupt_oam(BusAccessType access_type) {
    assert(mode == Mode::OamScan);
    assert(dot_counter >= 0 && dot_counter < 80);

    if (accessed_oam_row < 8) {
        return;
    }

    const auto read_word = [this](int offset) {
        assert(offset >= 0 && offset + 1 < static_cast<int>(oam.size()));
        return static_cast<uint16_t>(
            oam[offset] |
            static_cast<uint16_t>(oam[offset + 1]) << 8
        );
    };
    const auto write_word = [this](int offset, uint16_t value) {
        assert(offset >= 0 && offset + 1 < static_cast<int>(oam.size()));
        oam[offset] = value & 0xFF;
        oam[offset + 1] = value >> 8;
    };
    const auto copy_row = [this](int destination, int source) {
        assert(destination >= 0 && destination + 7 < static_cast<int>(oam.size()));
        assert(source >= 0 && source + 7 < static_cast<int>(oam.size()));
        for (int byte = 0; byte < 8; ++byte) {
            oam[destination + byte] = oam[source + byte];
        }
    };

    const int current_row = accessed_oam_row;

    if (
        access_type == BusAccessType::ReadAndInternal &&
        current_row >= 32 &&
        current_row < 152
    ) {
        const uint16_t a = read_word(current_row - 16);
        const uint16_t b = read_word(current_row - 8);
        const uint16_t c = read_word(current_row);
        const uint16_t d = read_word(current_row - 4);
        write_word(current_row - 8, static_cast<uint16_t>(
            (b & (a | c | d)) | (a & c & d)
        ));
        copy_row(current_row, current_row - 8);
        copy_row(current_row - 16, current_row - 8);
    }

    const bool read_corruption =
        access_type == BusAccessType::Read ||
        access_type == BusAccessType::ReadAndInternal;

    if (!read_corruption) {
        const uint16_t a = read_word(current_row);
        const uint16_t b = read_word(current_row - 8);
        const uint16_t c = read_word(current_row - 4);
        write_word(current_row, static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c));
        for (int byte = 2; byte < 8; ++byte) {
            oam[current_row + byte] = oam[current_row - 8 + byte];
        }
        return;
    }

    if ((current_row & 0x18) == 0x10 && current_row < 0x98) {
        const uint16_t a = read_word(current_row - 16);
        const uint16_t b = read_word(current_row - 8);
        const uint16_t c = read_word(current_row);
        const uint16_t d = read_word(current_row - 4);
        write_word(current_row - 8, static_cast<uint16_t>(
            (b & (a | c | d)) | (a & c & d)
        ));
        copy_row(current_row - 16, current_row - 8);
    } else if ((current_row & 0x18) == 0x00 && current_row < 0x98) {
        const uint16_t a = read_word(current_row);
        const uint16_t b = read_word(current_row - 4);
        const uint16_t c = read_word(current_row - 8);
        const uint16_t d = read_word(current_row - 16);
        const uint16_t e = read_word(current_row - 32);
        uint16_t value = 0;

        if (current_row == 0x40) {
            const uint16_t current = read_word(current_row);
            const uint16_t current_minus_four = read_word(current_row - 4);
            const uint16_t current_minus_six = read_word(current_row - 6);
            const uint16_t current_minus_eight = read_word(current_row - 8);
            const uint16_t f = read_word(current_row - 14);
            const uint16_t g = read_word(current_row - 16);
            const uint16_t h = read_word(current_row - 32);
            value = static_cast<uint16_t>(
                (
                    current_minus_eight &
                    (
                        h |
                        g |
                        static_cast<uint16_t>(~current_minus_six & f) |
                        current_minus_four |
                        current
                    )
                ) |
                (current_minus_four & g & h)
            );
        } else if (current_row == 0x20) {
            value = static_cast<uint16_t>(
                (c & (a | b | d | e)) | (a & b & d & e)
            );
        } else if (current_row == 0x60) {
            value = static_cast<uint16_t>(
                (c & (a | b | d | e)) | (b & d & e)
            );
        } else {
            value = static_cast<uint16_t>(c | (a & b & d & e));
        }

        write_word(current_row - 8, value);
        copy_row(current_row - 16, current_row - 8);
        copy_row(current_row - 32, current_row - 8);
    } else {
        const uint16_t a = read_word(current_row);
        const uint16_t b = read_word(current_row - 8);
        const uint16_t c = read_word(current_row - 4);
        const uint16_t value = static_cast<uint16_t>(b | (a & c));
        write_word(current_row - 8, value);
        write_word(current_row, value);
    }

    copy_row(current_row, current_row - 8);
    if (current_row == 0x80) {
        copy_row(0, current_row);
    }
}
