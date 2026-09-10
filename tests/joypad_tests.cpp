#include <catch2/catch_test_macros.hpp>

#include "interrupt_controller.h"
#include "keypad.h"

TEST_CASE("Joypad reset releases all buttons and selects no button group")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.write(0x10);
    joypad.set_button(JoypadButton::A, true);
    joypad.set_button(JoypadButton::Right, true);

    joypad.reset();

    REQUIRE(joypad.read() == 0xFF);

    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Right));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Left));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Up));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Down));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::A));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::B));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Select));
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::Start));
}

TEST_CASE("Joypad write preserves only selection bits")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();

    joypad.write(0x00);
    REQUIRE((joypad.read() & 0x30) == 0x00);
    REQUIRE((joypad.read() & 0xC0) == 0xC0);

    joypad.write(0xFF);
    REQUIRE((joypad.read() & 0x30) == 0x30);
    REQUIRE((joypad.read() & 0xC0) == 0xC0);

    joypad.write(0x0F);
    REQUIRE((joypad.read() & 0x30) == 0x00);
    REQUIRE((joypad.read() & 0xC0) == 0xC0);
}

TEST_CASE("Joypad reads no pressed buttons as high bits in selected groups")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();

    joypad.write(0x20);
    REQUIRE(joypad.read() == 0xEF);

    joypad.write(0x10);
    REQUIRE(joypad.read() == 0xDF);

    joypad.write(0x00);
    REQUIRE(joypad.read() == 0xCF);
}

TEST_CASE("Joypad maps d-pad buttons to the lower nibble when d-pad is selected")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x20);

    joypad.set_button(JoypadButton::Right, true);
    REQUIRE((joypad.read() & 0x0F) == 0x0E);

    joypad.set_button(JoypadButton::Left, true);
    REQUIRE((joypad.read() & 0x0F) == 0x0C);

    joypad.set_button(JoypadButton::Up, true);
    REQUIRE((joypad.read() & 0x0F) == 0x08);

    joypad.set_button(JoypadButton::Down, true);
    REQUIRE((joypad.read() & 0x0F) == 0x00);
}

TEST_CASE("Joypad maps action buttons to the lower nibble when action buttons are selected")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x10);

    joypad.set_button(JoypadButton::A, true);
    REQUIRE((joypad.read() & 0x0F) == 0x0E);

    joypad.set_button(JoypadButton::B, true);
    REQUIRE((joypad.read() & 0x0F) == 0x0C);

    joypad.set_button(JoypadButton::Select, true);
    REQUIRE((joypad.read() & 0x0F) == 0x08);

    joypad.set_button(JoypadButton::Start, true);
    REQUIRE((joypad.read() & 0x0F) == 0x00);
}

TEST_CASE("Joypad ignores d-pad buttons when only action buttons are selected")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x10);

    joypad.set_button(JoypadButton::Right, true);
    joypad.set_button(JoypadButton::Left, true);
    joypad.set_button(JoypadButton::Up, true);
    joypad.set_button(JoypadButton::Down, true);

    REQUIRE((joypad.read() & 0x0F) == 0x0F);
}

TEST_CASE("Joypad ignores action buttons when only d-pad is selected")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x20);

    joypad.set_button(JoypadButton::A, true);
    joypad.set_button(JoypadButton::B, true);
    joypad.set_button(JoypadButton::Select, true);
    joypad.set_button(JoypadButton::Start, true);

    REQUIRE((joypad.read() & 0x0F) == 0x0F);
}

TEST_CASE("Joypad combines selected groups with active-low lower bits")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x00);

    joypad.set_button(JoypadButton::Right, true);
    joypad.set_button(JoypadButton::B, true);
    joypad.set_button(JoypadButton::Start, true);

    REQUIRE(joypad.read() == 0xC4);
}

TEST_CASE("Joypad releasing a button restores its lower nibble bit")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();
    joypad.write(0x20);

    joypad.set_button(JoypadButton::Right, true);
    joypad.set_button(JoypadButton::Left, true);
    REQUIRE((joypad.read() & 0x0F) == 0x0C);

    joypad.set_button(JoypadButton::Right, false);
    REQUIRE((joypad.read() & 0x0F) == 0x0D);

    joypad.set_button(JoypadButton::Left, false);
    REQUIRE((joypad.read() & 0x0F) == 0x0F);
}

TEST_CASE("Joypad is_pressed reports current button state")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    joypad.reset();

    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::A));

    joypad.set_button(JoypadButton::A, true);
    REQUIRE(joypad.is_pressed(JoypadButton::A));

    joypad.set_button(JoypadButton::A, false);
    REQUIRE_FALSE(joypad.is_pressed(JoypadButton::A));
}

TEST_CASE("Joypad requests interrupt when selected button becomes pressed")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x10);
    joypad.reset();
    joypad.write(0x10);

    joypad.set_button(JoypadButton::A, true);

    REQUIRE((interrupts.read_if() & 0x10) == 0x10);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);
}

TEST_CASE("Joypad does not request interrupt when unselected button becomes pressed")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x10);
    joypad.reset();
    joypad.write(0x10);

    joypad.set_button(JoypadButton::Right, true);

    REQUIRE((interrupts.read_if() & 0x10) == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("Joypad selecting a group with an already pressed button requests interrupt")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x10);
    joypad.reset();
    joypad.write(0x30);
    joypad.set_button(JoypadButton::Start, true);

    REQUIRE((interrupts.read_if() & 0x10) == 0x00);

    joypad.write(0x10);

    REQUIRE((interrupts.read_if() & 0x10) == 0x10);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);
}

TEST_CASE("Joypad releasing a selected button does not request interrupt")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x10);
    joypad.reset();
    joypad.write(0x20);
    joypad.set_button(JoypadButton::Down, true);
    interrupts.write_if(0x00);

    joypad.set_button(JoypadButton::Down, false);

    REQUIRE((interrupts.read_if() & 0x10) == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("Joypad pressing an already pressed selected button does not request another transition")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x10);
    joypad.reset();
    joypad.write(0x20);
    joypad.set_button(JoypadButton::Up, true);
    interrupts.write_if(0x00);

    joypad.set_button(JoypadButton::Up, true);

    REQUIRE((interrupts.read_if() & 0x10) == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("Joypad keeps both groups connected during the DMG selection delay")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    joypad.reset();
    joypad.write(0x20);
    joypad.set_button(JoypadButton::Right, true);

    joypad.write(0x10);
    REQUIRE((joypad.read() & 0x30) == 0x10);
    REQUIRE((joypad.read() & 0x01) == 0x00);

    joypad.tick_dots(23);
    REQUIRE((joypad.read() & 0x01) == 0x00);

    joypad.tick_dots(1);
    REQUIRE((joypad.read() & 0x01) == 0x01);
}

TEST_CASE("Joypad uses the shorter DMG delay when switching to the d-pad")
{
    InterruptController interrupts;
    Joypad joypad(interrupts);

    interrupts.reset();
    joypad.reset();
    joypad.write(0x10);
    joypad.set_button(JoypadButton::A, true);

    joypad.write(0x20);
    joypad.tick_dots(11);
    REQUIRE((joypad.read() & 0x01) == 0x00);

    joypad.tick_dots(1);
    REQUIRE((joypad.read() & 0x01) == 0x01);
}
