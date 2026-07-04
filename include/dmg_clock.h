#pragma once

#include <cstdint>

namespace dmg {

static constexpr uint32_t dot_clock_hz = 4'194'304;
static constexpr int dots_per_m_cycle = 4;
static constexpr int dots_per_scanline = 456;
static constexpr int visible_scanlines = 144;
static constexpr int total_scanlines = 154;
static constexpr int dots_per_frame = dots_per_scanline * total_scanlines;

constexpr int dots_from_m_cycles(int cycles)
{
    return cycles * dots_per_m_cycle;
}

}
