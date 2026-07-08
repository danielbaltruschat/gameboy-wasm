#include "keypad.h"

#include "interrupt_controller.h"

#include <cstddef>

Joypad::Joypad(InterruptController& interrupts) : interrupts(interrupts)
{
    reset();
}

void Joypad::reset() {
    select_bits = 0x30;
    buttons.fill(false);
}

uint8_t Joypad::read() const {
    return 0xC0 | select_bits | lower_nibble();
}

void Joypad::write(uint8_t value)
{
    const uint8_t previous_value = read();

    select_bits = value & 0x30;

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

void Joypad::request_interrupt_if_needed(uint8_t previous_value, uint8_t next_value)
{
    const uint8_t pressed_transitions = previous_value & static_cast<uint8_t>(~next_value) & 0x0F;

    if (pressed_transitions != 0) {
        interrupts.request(Interrupt::Joypad);
    }
}
