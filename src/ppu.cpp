#include "ppu.h"
#include "interrupt_controller.h"
#include <algorithm>
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
    mode3_dot_target = 172;
    scx_discard_dots = 0;
    object_fetch_dots_remaining = 0;
    object_fetch_index = -1;
    screen_x = 0;
    scanline = 0;
    oam_scan_index = 0;
    window_line = 0;
    window_y_triggered = false;
    window_triggered_this_line = false;
    first_frame_blank = true;
    oam_dma_active = false;

    mode = Mode::HBlank;

    fetcher = PixelFetcher{};
    bg_fifo = PixelFifo{};
    obj_fifo = PixelFifo{};
    line_objects.fill(ObjectCandidate{});
    line_object_fetched.fill(false);
    line_object_count = 0;

    stat_interrupt_line = false;
}

void PPU::tick_dots(int dots) {
    assert(dots >= 0);
    assert(dot_counter >= 0 && dot_counter < 456);
    assert(scanline <= 153);
    assert(ly <= 153);

    if (!lcd_enabled() || dots == 0) {
        return;
    }

    for (int dot = 0; dot < dots; ++dot) {
        dot_counter++;

        if (mode == Mode::OamScan) {
            if ((dot_counter & 0x01) == 0) {
                scan_oam_entry();
            }

            if (dot_counter == 80) {
                mode3_dot_target = calculate_mode3_dot_target();
                set_mode(Mode::Drawing);
            }
        }

        if (mode == Mode::Drawing) {
            if (dot_counter < 80 + mode3_dot_target) {
                if (!tick_object_fetch()) {
                    tick_pixel_fetcher();
                    mix_and_push_pixel();
                }
            } else {
                if (window_triggered_this_line) {
                    window_line++;
                }
                set_mode(Mode::HBlank);
            }
        }

        if (scanline == 153 && dot_counter == 4) {
            ly = 0;
            update_stat_interrupt();
        }

        if (dot_counter < 456) {
            continue;
        }

        dot_counter = 0;
        scanline = static_cast<uint8_t>((scanline + 1) % 154);
        ly = scanline;

        if (scanline == 0) {
            window_line = 0;
            window_y_triggered = false;
            set_mode(Mode::OamScan);
            begin_scanline();
        } else if (scanline == 144) {
            set_mode(Mode::VBlank);
            interrupts.request(Interrupt::VBlank);
            frame_ready = true;
            first_frame_blank = false;
        } else if (scanline < 144) {
            set_mode(Mode::OamScan);
            begin_scanline();
        } else {
            set_mode(Mode::VBlank);
        }
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
    return !lcd_enabled() || mode != Mode::Drawing;
}

bool PPU::oam_accessible() const {
    return !lcd_enabled() || mode == Mode::HBlank || mode == Mode::VBlank;
}

bool PPU::window_enabled() const {
    return
        window_y_triggered &&
        (lcdc & 0x21) == 0x21 &&
        wx <= 166;
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
                scanline = 0;
                dot_counter = 0;
                screen_x = 0;
                window_line = 0;
                window_y_triggered = false;
                window_triggered_this_line = false;
                first_frame_blank = true;
                framebuffer = Framebuffer{};
                set_mode(Mode::HBlank);
                clear_fifos();
            } else if (!was_enabled && lcd_enabled()) {
                ly = 0;
                scanline = 0;
                dot_counter = 0;
                window_line = 0;
                window_y_triggered = false;
                first_frame_blank = true;
                framebuffer = Framebuffer{};
                set_mode(Mode::OamScan);
                begin_scanline();
            }
            break;
        }
        case 0xFF41: {
            if (lcd_enabled()) {
                const bool temporary_line = stat_interrupt_active(0x78);
                if (temporary_line && !stat_interrupt_line) {
                    interrupts.request(Interrupt::LCDStat);
                }
                stat_interrupt_line = temporary_line;
            }

            stat = value & 0x78;
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

void PPU::notify_oam_bus_access(uint16_t addr, BusAccessType access_type) {
    assert(addr >= 0xFE00 && addr <= 0xFEFF);

    if (
        lcd_enabled() &&
        mode == Mode::OamScan &&
        !oam_dma_active
    ) {
        corrupt_oam(access_type);
    }
}

bool PPU::stat_interrupt_active(uint8_t interrupt_selects) const {
    return
        lcd_enabled() &&
        (
            ((interrupt_selects & 0x40) != 0 && ly == lyc) ||
            ((interrupt_selects & 0x20) != 0 && mode == Mode::OamScan) ||
            ((interrupt_selects & 0x10) != 0 && mode == Mode::VBlank) ||
            ((interrupt_selects & 0x08) != 0 && mode == Mode::HBlank)
        );
}

void PPU::update_stat_interrupt() {
    const bool interrupt_line = stat_interrupt_active(stat);

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

void PPU::clear_fifos() {
    bg_fifo = {};
    obj_fifo = {};
}

void PPU::begin_scanline() {
    assert(lcd_enabled());
    assert(ly < 144);
    assert(mode == Mode::OamScan);

    screen_x = 0;
    scx_discard_dots = scx & 0x07;
    object_fetch_dots_remaining = 0;
    object_fetch_index = -1;
    oam_scan_index = 0;
    if (ly == wy) {
        window_y_triggered = true;
    }
    window_triggered_this_line = false;
    fetcher = {};
    fetcher.y = static_cast<uint8_t>(scy + ly);
    clear_fifos();
    obj_fifo.size = obj_fifo.pixels.size();
    line_objects.fill(ObjectCandidate{});
    line_object_fetched.fill(false);
    line_object_count = 0;
}

void PPU::scan_oam_entry() {
    assert(ly < 144);
    assert(oam_scan_index < 40);

    const int object_height = (lcdc & 0x04) != 0 ? 16 : 8;
    const uint16_t offset = static_cast<uint16_t>(oam_scan_index) * 4;
    const uint8_t object_y = oam_dma_active ? 0xFF : oam[offset];
    const int object_top = static_cast<int>(object_y) - 16;

    if (
        line_object_count < line_objects.size() &&
        ly >= object_top &&
        ly < object_top + object_height
    ) {
        ObjectCandidate& object = line_objects[line_object_count++];
        object.y = object_y;
        object.x = oam_dma_active ? 0xFF : oam[offset + 1];
        object.oam_index = oam_scan_index;
    }

    oam_scan_index++;
}

int PPU::calculate_mode3_dot_target() {
    int target = 172 + scx_discard_dots;
    const bool use_window = window_enabled();
    const int window_origin_x = static_cast<int>(wx) - 7;

    if (use_window) {
        target += 6;
        if (window_origin_x < 0) {
            target -= window_origin_x;
        }
        if (wx == 0 && scx_discard_dots > 0) {
            target--;
        }
    }

    if ((lcdc & 0x02) == 0) {
        return target;
    }

    std::array<uint8_t, 10> order{};
    for (uint8_t index = 0; index < line_object_count; ++index) {
        order[index] = index;
    }
    std::stable_sort(
        order.begin(),
        order.begin() + line_object_count,
        [this](uint8_t left, uint8_t right) {
            return line_objects[left].x < line_objects[right].x;
        }
    );

    std::array<int, 10> considered_tiles{};
    uint8_t considered_tile_count = 0;

    for (uint8_t order_index = 0; order_index < line_object_count; ++order_index) {
        ObjectCandidate& object = line_objects[order[order_index]];
        if (object.x >= 168) {
            continue;
        }

        if (object.x == 0) {
            object.fetch_penalty = 11;
            target += object.fetch_penalty;
            continue;
        }

        const int object_left = static_cast<int>(object.x) - 8;
        const bool over_window = use_window && object_left >= window_origin_x;
        const int tile_coordinate = over_window
            ? object_left - window_origin_x
            : object_left + scx;
        const int tile_number = tile_coordinate >= 0
            ? tile_coordinate / 8
            : (tile_coordinate - 7) / 8;
        const int tile_key = tile_number + (over_window ? 0x100 : 0);
        const auto considered_end = considered_tiles.begin() + considered_tile_count;
        const bool tile_was_considered = std::find(
            considered_tiles.begin(),
            considered_end,
            tile_key
        ) != considered_end;

        int penalty = 6;
        if (!tile_was_considered) {
            int pixel_in_tile = tile_coordinate % 8;
            if (pixel_in_tile < 0) {
                pixel_in_tile += 8;
            }
            penalty += std::max(0, 5 - pixel_in_tile);
            considered_tiles[considered_tile_count++] = tile_key;
        }

        object.fetch_penalty = static_cast<uint8_t>(penalty);
        target += penalty;
    }

    return target;
}

void PPU::tick_pixel_fetcher() {
    assert(mode == Mode::Drawing);

    if (screen_x >= Framebuffer::width) {
        return;
    }

    fetcher.step++;

    if (fetcher.step == 2) {
        const uint16_t map_base = fetcher.fetching_window
            ? ((lcdc & 0x40) != 0 ? 0x1C00 : 0x1800)
            : ((lcdc & 0x08) != 0 ? 0x1C00 : 0x1800);
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        const uint8_t tile_x = fetcher.fetching_window
            ? fetcher.x & 0x1F
            : static_cast<uint8_t>((scx / 8 + fetcher.x) & 0x1F);
        const uint8_t tile_y = (pixel_y / 8) & 0x1F;

        fetcher.y = pixel_y;
        fetcher.tile_id = vram[map_base + tile_y * 32 + tile_x];
    } else if (fetcher.step == 4) {
        const int tile_base = (lcdc & 0x10) != 0
            ? static_cast<int>(fetcher.tile_id) * 16
            : 0x1000 + static_cast<int8_t>(fetcher.tile_id) * 16;
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        const int row_offset = (pixel_y & 0x07) * 2;

        fetcher.tile_data_low = vram[tile_base + row_offset];
    } else if (fetcher.step == 6) {
        const int tile_base = (lcdc & 0x10) != 0
            ? static_cast<int>(fetcher.tile_id) * 16
            : 0x1000 + static_cast<int8_t>(fetcher.tile_id) * 16;
        const uint8_t pixel_y = fetcher.fetching_window
            ? window_line
            : static_cast<uint8_t>(scy + ly);
        const int row_offset = (pixel_y & 0x07) * 2;

        fetcher.tile_data_high = vram[tile_base + row_offset + 1];
    } else if (fetcher.step >= 8) {
        if (bg_fifo.size <= 8) {
            push_bg_pixels();
            fetcher.x++;
            fetcher.step = 0;
        } else {
            fetcher.step = 7;
        }
    }
}

void PPU::push_bg_pixels() {
    assert(bg_fifo.size <= 8);

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

void PPU::fetch_object_pixels(const ObjectCandidate& object) {
    assert(obj_fifo.size == obj_fifo.pixels.size());

    const int object_height = (lcdc & 0x04) != 0 ? 16 : 8;
    const uint16_t object_offset = static_cast<uint16_t>(object.oam_index) * 4;
    const uint8_t object_tile = oam_dma_active ? 0xFF : oam[object_offset + 2];
    const uint8_t object_attributes = oam_dma_active ? 0xFF : oam[object_offset + 3];
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

    const uint16_t tile_address =
        static_cast<uint16_t>(tile) * 16 +
        static_cast<uint16_t>(row) * 2;
    const uint8_t tile_data_low = vram[tile_address];
    const uint8_t tile_data_high = vram[tile_address + 1];
    const int object_left = static_cast<int>(object.x) - 8;

    for (int pixel = 0; pixel < 8; ++pixel) {
        const int screen_position = object_left + pixel;
        const int fifo_offset = screen_position - screen_x;

        if (fifo_offset < 0 || fifo_offset >= static_cast<int>(obj_fifo.pixels.size())) {
            continue;
        }

        const uint8_t bit = (object_attributes & 0x20) != 0
            ? static_cast<uint8_t>(pixel)
            : static_cast<uint8_t>(7 - pixel);
        const uint8_t color =
            ((tile_data_high >> bit) & 0x01) << 1 |
            ((tile_data_low >> bit) & 0x01);

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

bool PPU::tick_object_fetch() {
    if (object_fetch_index >= 0) {
        assert(object_fetch_dots_remaining > 0);

        object_fetch_dots_remaining--;
        if (object_fetch_dots_remaining == 0) {
            if ((lcdc & 0x02) != 0) {
                fetch_object_pixels(line_objects[object_fetch_index]);
            }
            object_fetch_index = -1;
        }
        return true;
    }

    if ((lcdc & 0x02) == 0 || screen_x >= Framebuffer::width) {
        return false;
    }

    int next_object = -1;
    for (uint8_t index = 0; index < line_object_count; ++index) {
        const ObjectCandidate& object = line_objects[index];
        const int object_left = static_cast<int>(object.x) - 8;
        const bool reached_object =
            object_left == screen_x ||
            (screen_x == 0 && object_left < 0);

        if (
            line_object_fetched[index] ||
            object.fetch_penalty == 0 ||
            !reached_object
        ) {
            continue;
        }

        if (
            next_object < 0 ||
            object.x < line_objects[next_object].x ||
            (
                object.x == line_objects[next_object].x &&
                object.oam_index < line_objects[next_object].oam_index
            )
        ) {
            next_object = index;
        }
    }

    if (next_object < 0) {
        return false;
    }

    line_object_fetched[next_object] = true;
    object_fetch_index = next_object;
    object_fetch_dots_remaining = line_objects[next_object].fetch_penalty;
    return tick_object_fetch();
}

void PPU::mix_and_push_pixel() {
    if (screen_x >= Framebuffer::width) {
        return;
    }

    const bool use_window = window_enabled();
    const int window_x = wx < 7 ? 0 : wx - 7;

    if (
        use_window &&
        !window_triggered_this_line &&
        screen_x >= window_x
    ) {
        window_triggered_this_line = true;
        bg_fifo = {};
        fetcher = {};
        fetcher.y = window_line;
        fetcher.fetching_window = true;
        scx_discard_dots = wx < 7 ? 7 - wx : 0;
        return;
    }

    if (bg_fifo.size == 0) {
        return;
    }

    const FifoPixel background_pixel = bg_fifo.pixels[bg_fifo.head];
    bg_fifo.head = (bg_fifo.head + 1) % bg_fifo.pixels.size();
    bg_fifo.size--;

    if (scx_discard_dots > 0) {
        scx_discard_dots--;
        return;
    }

    const uint8_t background_color =
        (lcdc & 0x01) != 0 ? background_pixel.color : 0;
    const bool objects_enabled = (lcdc & 0x02) != 0;

    const FifoPixel object_pixel = obj_fifo.pixels[obj_fifo.head];
    obj_fifo.pixels[obj_fifo.head] = {};
    obj_fifo.head = (obj_fifo.head + 1) % obj_fifo.pixels.size();

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
}

void PPU::corrupt_oam(BusAccessType access_type) {
    assert(mode == Mode::OamScan);
    assert(dot_counter >= 0 && dot_counter < 80);

    const int row = dot_counter / 4;
    if (row == 0) {
        return;
    }

    const auto read_word = [this](int offset) {
        return static_cast<uint16_t>(
            oam[offset] |
            static_cast<uint16_t>(oam[offset + 1]) << 8
        );
    };
    const auto write_word = [this](int offset, uint16_t value) {
        oam[offset] = value & 0xFF;
        oam[offset + 1] = value >> 8;
    };

    if (
        access_type == BusAccessType::ReadAndInternal &&
        row >= 4 &&
        row < 19
    ) {
        const int two_rows_before = (row - 2) * 8;
        const int previous_row = (row - 1) * 8;
        const int current_row = row * 8;
        const uint16_t a = read_word(two_rows_before);
        const uint16_t b = read_word(previous_row);
        const uint16_t c = read_word(current_row);
        const uint16_t d = read_word(previous_row + 4);
        const uint16_t corrupted_previous_word = static_cast<uint16_t>(
            (b & (a | c | d)) | (a & c & d)
        );

        write_word(previous_row, corrupted_previous_word);
        for (int word = 0; word < 4; ++word) {
            const uint16_t value = read_word(previous_row + word * 2);
            write_word(current_row + word * 2, value);
            write_word(two_rows_before + word * 2, value);
        }
    }

    const int current_row = row * 8;
    const int previous_row = current_row - 8;
    const uint16_t a = read_word(current_row);
    const uint16_t b = read_word(previous_row);
    const uint16_t c = read_word(previous_row + 4);
    const bool read_corruption =
        access_type == BusAccessType::Read ||
        access_type == BusAccessType::ReadAndInternal;
    const uint16_t corrupted_first_word = read_corruption
        ? static_cast<uint16_t>(b | (a & c))
        : static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c);

    write_word(current_row, corrupted_first_word);
    for (int word = 1; word < 4; ++word) {
        write_word(current_row + word * 2, read_word(previous_row + word * 2));
    }
}
