#include "keypad.h"

#include "interrupt_controller.h"

#include <cassert>
#include <cstddef>

Joypad::Joypad(InterruptController& interrupts) : interrupts(interrupts)
{
    reset();
}

void Joypad::reset() {
    select_bits = 0x30;
    pending_select_bits = 0x30;
    switch_dots_remaining = 0;
    buttons.fill(false);
}

void Joypad::tick_dots(int dots) {
    assert(dots >= 0);

    if (switch_dots_remaining == 0 || dots == 0) {
        return;
    }

    if (dots < switch_dots_remaining) {
        switch_dots_remaining -= dots;
        return;
    }

    const uint8_t previous_value = read();
    select_bits = pending_select_bits;
    switch_dots_remaining = 0;
    request_interrupt_if_needed(previous_value, read());
}

uint8_t Joypad::read() const {
    const uint8_t visible_select = switch_dots_remaining == 0
        ? select_bits
        : pending_select_bits;
    return 0xC0 | visible_select | lower_nibble();
}

void Joypad::write(uint8_t value)
{
    const uint8_t previous_value = read();
    const uint8_t next_select = value & 0x30;

    if (switch_dots_remaining != 0) {
        select_bits = pending_select_bits;
        switch_dots_remaining = 0;
    }

    pending_select_bits = next_select;
    switch_dots_remaining = switching_delay(select_bits, next_select);
    select_bits = switch_dots_remaining == 0
        ? next_select
        : static_cast<uint8_t>(select_bits & next_select);

    request_interrupt_if_needed(previous_value, read());
}

void Joypad::set_button(JoypadButton button, bool pressed)
{
    const uint8_t previous_value = read();

    buttons[static_cast<std::size_t>(button)] = pressed;

    request_interrupt_if_needed(previous_value, read());
}

bool Joypad::is_pressed(JoypadButton button) const
{
    return buttons[static_cast<std::size_t>(button)];
}

uint8_t Joypad::lower_nibble() const
{
    uint8_t value = 0x0F;

    const bool dpad_selected = (select_bits & 0x10) == 0;
    const bool action_selected = (select_bits & 0x20) == 0;

    if (dpad_selected) {
        if (is_pressed(JoypadButton::Right)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Right));
        if (is_pressed(JoypadButton::Left)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Left));
        if (is_pressed(JoypadButton::Up)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Up));
        if (is_pressed(JoypadButton::Down)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Down));
    }

    if (action_selected) {
        if (is_pressed(JoypadButton::A)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::A));
        if (is_pressed(JoypadButton::B)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::B));
        if (is_pressed(JoypadButton::Select)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Select));
        if (is_pressed(JoypadButton::Start)) value &= static_cast<uint8_t>(~button_bit(JoypadButton::Start));
    }

    return value;
}

uint8_t Joypad::button_bit(JoypadButton button) const
{
    switch (button) {
    case JoypadButton::Right:
    case JoypadButton::A:
        return 0x01;
    case JoypadButton::Left:
    case JoypadButton::B:
        return 0x02;
    case JoypadButton::Up:
    case JoypadButton::Select:
        return 0x04;
    case JoypadButton::Down:
    case JoypadButton::Start:
        return 0x08;
    default:
        return 0x00;
    }
}

int Joypad::switching_delay(uint8_t previous_select, uint8_t next_select) const
{
    const uint8_t transition = static_cast<uint8_t>(
        (previous_select >> 4) | (next_select >> 2)
    );

    switch (transition) {
    case 0x04:
    case 0x06:
    case 0x0C:
    case 0x0E:
        return 48;
    case 0x08:
    case 0x09:
    case 0x0D:
        return 24;
    default:
        return 0;
    }
}

void Joypad::request_interrupt_if_needed(uint8_t previous_value, uint8_t next_value)
{
    const uint8_t pressed_transitions = previous_value & static_cast<uint8_t>(~next_value) & 0x0F;

    if (pressed_transitions != 0) {
        interrupts.request(Interrupt::Joypad);
    }
}
