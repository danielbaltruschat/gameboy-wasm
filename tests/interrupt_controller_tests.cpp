#include <catch2/catch_test_macros.hpp>

#include "interrupt_controller.h"

TEST_CASE("InterruptController reset clears IF and IE")
{
    InterruptController interrupts;

    interrupts.write_if(0x1F);
    interrupts.write_ie(0x1F);

    interrupts.reset();

    REQUIRE(interrupts.read_if() == 0xE0);
    REQUIRE(interrupts.read_ie() == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("InterruptController IF reads upper bits as set")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0x00);
    REQUIRE(interrupts.read_if() == 0xE0);

    interrupts.write_if(0x1F);
    REQUIRE(interrupts.read_if() == 0xFF);
}

TEST_CASE("InterruptController writes only preserve lower five IF bits")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0xFF);

    REQUIRE(interrupts.read_if() == 0xFF);

    interrupts.write_if(0xA0);

    REQUIRE(interrupts.read_if() == 0xE0);
}

TEST_CASE("InterruptController IE preserves all eight storage bits")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_ie(0xFF);

    REQUIRE(interrupts.read_ie() == 0xFF);

    interrupts.write_ie(0xA0);

    REQUIRE(interrupts.read_ie() == 0xA0);
}

TEST_CASE("InterruptController request sets the matching IF bit")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Timer);

    REQUIRE((interrupts.read_if() & 0x04) == 0x04);
    REQUIRE((interrupts.read_if() & 0x1B) == 0x00);
}

TEST_CASE("InterruptController acknowledge clears only the selected IF bit")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::VBlank);
    interrupts.request(Interrupt::Timer);
    interrupts.request(Interrupt::Joypad);

    interrupts.acknowledge(Interrupt::Timer);

    REQUIRE((interrupts.read_if() & 0x01) == 0x01);
    REQUIRE((interrupts.read_if() & 0x04) == 0x00);
    REQUIRE((interrupts.read_if() & 0x10) == 0x10);
}

TEST_CASE("InterruptController pending interrupt requires both IF and IE bits")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Timer);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());

    interrupts.write_ie(0x04);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("InterruptController returns highest priority pending interrupt")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Joypad);
    interrupts.request(Interrupt::Serial);
    interrupts.request(Interrupt::Timer);
    interrupts.write_ie(0x1F);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);

    interrupts.request(Interrupt::VBlank);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);
}

TEST_CASE("InterruptController priority advances after acknowledging")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::VBlank);
    interrupts.request(Interrupt::LCDStat);
    interrupts.request(Interrupt::Timer);
    interrupts.write_ie(0x07);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);

    interrupts.acknowledge(Interrupt::VBlank);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::LCDStat);

    interrupts.acknowledge(Interrupt::LCDStat);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("InterruptController ignores requested interrupts that are not enabled")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::VBlank);
    interrupts.request(Interrupt::Timer);
    interrupts.request(Interrupt::Joypad);

    interrupts.write_ie(0x10);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);

    interrupts.write_ie(0x14);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);

    interrupts.write_ie(0x15);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);
}

TEST_CASE("InterruptController software-written IF bits participate in priority")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0x18);
    interrupts.write_ie(0x1F);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Serial);

    interrupts.write_if(0x1A);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::LCDStat);
}

TEST_CASE("InterruptController software can clear IF while IE remains enabled")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Timer);
    interrupts.write_ie(0x04);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);

    interrupts.write_if(0x00);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
    REQUIRE(interrupts.read_ie() == 0x04);
}

TEST_CASE("InterruptController enabling IE after IF request exposes pending interrupt")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Serial);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());

    interrupts.write_ie(0x08);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Serial);
}

TEST_CASE("InterruptController disabling IE hides but does not clear IF")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Joypad);
    interrupts.write_ie(0x10);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);

    interrupts.write_ie(0x00);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
    REQUIRE((interrupts.read_if() & 0x10) == 0x10);

    interrupts.write_ie(0x10);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);
}

TEST_CASE("InterruptController repeated requests keep IF bit set")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::Timer);
    interrupts.request(Interrupt::Timer);
    interrupts.write_ie(0x04);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);

    interrupts.acknowledge(Interrupt::Timer);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
    REQUIRE((interrupts.read_if() & 0x04) == 0x00);
}

TEST_CASE("InterruptController acknowledging a non-requested interrupt is a no-op for other flags")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.request(Interrupt::VBlank);
    interrupts.request(Interrupt::Serial);
    interrupts.write_ie(0x09);

    interrupts.acknowledge(Interrupt::Timer);

    REQUIRE((interrupts.read_if() & 0x01) == 0x01);
    REQUIRE((interrupts.read_if() & 0x08) == 0x08);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);
}

TEST_CASE("InterruptController drains all enabled pending interrupts in priority order")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0x1F);
    interrupts.write_ie(0x1F);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);
    interrupts.acknowledge(Interrupt::VBlank);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::LCDStat);
    interrupts.acknowledge(Interrupt::LCDStat);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
    interrupts.acknowledge(Interrupt::Timer);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Serial);
    interrupts.acknowledge(Interrupt::Serial);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Joypad);
    interrupts.acknowledge(Interrupt::Joypad);

    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
    REQUIRE(interrupts.read_if() == 0xE0);
}

TEST_CASE("InterruptController pending priority changes when high-priority IE bit is disabled")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0x07);
    interrupts.write_ie(0x07);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::VBlank);

    interrupts.write_ie(0x06);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::LCDStat);

    interrupts.write_ie(0x04);

    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("InterruptController IF upper-bit writes do not create pending interrupts")
{
    InterruptController interrupts;

    interrupts.reset();
    interrupts.write_if(0xE0);
    interrupts.write_ie(0x1F);

    REQUIRE(interrupts.read_if() == 0xE0);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}
