#pragma once

#include <array>
#include <cstdint>

#include "interrupt_controller.h"

enum class JoypadButton {
    Right,
    Left,
    Up,
    Down,
    A,
    B,
    Select,
    Start,
};

class Joypad {
public:
    explicit Joypad(InterruptController& interrupts);

    void reset();

    uint8_t read() const;        // FF00 / P1 / JOYP
    void write(uint8_t value);   // only bits 4-5 are meaningful

    void set_button(JoypadButton button, bool pressed);
    bool is_pressed(JoypadButton button) const;

private:
    InterruptController& interrupts;

    // Bits 4-5 written by CPU. Active-low selection:
    // bit 5 = action buttons, bit 4 = d-pad.
    uint8_t select_bits = 0x30;

    std::array<bool, 8> buttons{};

    uint8_t lower_nibble() const;
    uint8_t button_bit(JoypadButton button) const;
    void request_interrupt_if_needed(uint8_t previous_value, uint8_t next_value);
};
