#include <catch2/catch_test_macros.hpp>

#include "interrupt_controller.h"
#include "timer.h"

TEST_CASE("Timer reset clears memory-mapped timer registers")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.write(0xFF05, 0x12);
    timer.write(0xFF06, 0x34);
    timer.write(0xFF07, 0x07);

    timer.reset();

    REQUIRE(timer.read(0xFF04) == 0x00);
    REQUIRE(timer.read(0xFF05) == 0x00);
    REQUIRE(timer.read(0xFF06) == 0x00);
    REQUIRE(timer.read(0xFF07) == 0xF8);
    REQUIRE(timer.take_div_apu_ticks() == 0);
}

TEST_CASE("Timer TIMA and TMA are readable and writable")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.write(0xFF05, 0x9A);
    timer.write(0xFF06, 0xBC);

    REQUIRE(timer.read(0xFF05) == 0x9A);
    REQUIRE(timer.read(0xFF06) == 0xBC);
}

TEST_CASE("Timer TAC preserves only timer control bits")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.write(0xFF07, 0xFF);

    REQUIRE(timer.read(0xFF07) == 0xFF);

    timer.write(0xFF07, 0x00);

    REQUIRE(timer.read(0xFF07) == 0xF8);
}

TEST_CASE("Timer DIV exposes the upper byte of the internal divider")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(255);
    REQUIRE(timer.read(0xFF04) == 0x00);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF04) == 0x01);

    timer.tick_dots(256);
    REQUIRE(timer.read(0xFF04) == 0x02);
}

TEST_CASE("Timer writing DIV resets the internal divider")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(0x1234);
    REQUIRE(timer.read(0xFF04) == 0x12);

    timer.write(0xFF04, 0xFF);

    REQUIRE(timer.read(0xFF04) == 0x00);
}

TEST_CASE("Timer DIV wraps after 65536 dots")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(0xFFFF);
    REQUIRE(timer.read(0xFF04) == 0xFF);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF04) == 0x00);
}

TEST_CASE("Timer does not increment TIMA while disabled")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF05, 0x10);
    timer.write(0xFF07, 0x01);

    timer.tick_dots(1024);

    REQUIRE(timer.read(0xFF05) == 0x10);
}

TEST_CASE("Timer enabling while selected divider bit is low does not increment TIMA")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF05, 0x22);
    timer.tick_dots(4);

    timer.write(0xFF07, 0x05);

    REQUIRE(timer.read(0xFF05) == 0x22);
}

TEST_CASE("Timer frequency 4096 Hz increments TIMA every 1024 dots")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x04);

    timer.tick_dots(1023);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer frequency 262144 Hz increments TIMA every 16 dots")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);

    timer.tick_dots(15);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer frequency 65536 Hz increments TIMA every 64 dots")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x06);

    timer.tick_dots(63);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer frequency 16384 Hz increments TIMA every 256 dots")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x07);

    timer.tick_dots(255);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.tick_dots(1);
    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer accumulates multiple TIMA increments from a single tick")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);

    timer.tick_dots(64);

    REQUIRE(timer.read(0xFF05) == 0x04);
}

TEST_CASE("Timer carries partial dots between ticks")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);

    timer.tick_dots(8);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.tick_dots(8);
    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer writing DIV can create a falling edge and increment TIMA")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);

    timer.tick_dots(8);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.write(0xFF04, 0x00);

    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer changing TAC without a falling edge preserves TIMA")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF05, 0x44);
    timer.tick_dots(4);

    timer.write(0xFF07, 0x04);

    REQUIRE(timer.read(0xFF05) == 0x44);
}

TEST_CASE("Timer writing TAC can create a falling edge and increment TIMA")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);

    timer.tick_dots(8);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.write(0xFF07, 0x00);

    REQUIRE(timer.read(0xFF05) == 0x01);
}

TEST_CASE("Timer reset clears previous edge state")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();
    timer.write(0xFF07, 0x05);
    timer.tick_dots(8);
    REQUIRE(timer.read(0xFF05) == 0x00);

    timer.reset();
    timer.write(0xFF07, 0x05);
    timer.tick_dots(8);

    REQUIRE(timer.read(0xFF05) == 0x00);
}

TEST_CASE("Timer overflow reloads TIMA from TMA and requests interrupt after one M-cycle")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x04);
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(16);

    REQUIRE(timer.read(0xFF05) == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());

    timer.tick_dots(4);

    REQUIRE(timer.read(0xFF05) == 0xA7);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("Timer overflow delay is counted in dots across smaller ticks")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x04);
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0x3C);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(16);
    timer.tick_dots(3);

    REQUIRE(timer.read(0xFF05) == 0x00);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());

    timer.tick_dots(1);

    REQUIRE(timer.read(0xFF05) == 0x3C);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("Timer overflow requests IF even when timer interrupt is disabled in IE")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(20);

    REQUIRE((interrupts.read_if() & 0x04) == 0x04);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("Timer writing TIMA during overflow delay cancels reload and interrupt")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x04);
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(16);
    timer.write(0xFF05, 0x55);
    timer.tick_dots(4);

    REQUIRE(timer.read(0xFF05) == 0x55);
    REQUIRE_FALSE(interrupts.highest_priority_pending().has_value());
}

TEST_CASE("Timer ignores a TIMA write during the reload cycle")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x04);
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(20);
    timer.write(0xFF05, 0x55);

    REQUIRE(timer.read(0xFF05) == 0xA7);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("Timer accepts a TIMA write after the reload cycle")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(20);
    timer.tick_dots(1);
    timer.write(0xFF05, 0x55);

    REQUIRE(timer.read(0xFF05) == 0x55);
    REQUIRE((interrupts.read_if() & 0x04) != 0);
}

TEST_CASE("Timer writing TMA during reload also updates TIMA")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(20);
    timer.write(0xFF06, 0x55);

    REQUIRE(timer.read(0xFF06) == 0x55);
    REQUIRE(timer.read(0xFF05) == 0x55);
}

TEST_CASE("Timer writing TMA during overflow delay changes the reload value")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    interrupts.reset();
    interrupts.write_ie(0x04);
    timer.reset();
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF06, 0xA7);
    timer.write(0xFF07, 0x05);

    timer.tick_dots(16);
    timer.write(0xFF06, 0x55);
    timer.tick_dots(4);

    REQUIRE(timer.read(0xFF05) == 0x55);
    REQUIRE(interrupts.highest_priority_pending() == Interrupt::Timer);
}

TEST_CASE("Timer reports DIV-APU ticks on 512 Hz divider falling edges")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(8191);
    REQUIRE(timer.take_div_apu_ticks() == 0);

    timer.tick_dots(1);
    REQUIRE(timer.take_div_apu_ticks() == 1);
    REQUIRE(timer.take_div_apu_ticks() == 0);
}

TEST_CASE("Timer writing DIV can create a DIV-APU tick")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(4096);
    REQUIRE(timer.take_div_apu_ticks() == 0);

    timer.write(0xFF04, 0x00);

    REQUIRE(timer.take_div_apu_ticks() == 1);
    REQUIRE(timer.read(0xFF04) == 0x00);
}

TEST_CASE("Timer accumulates multiple DIV-APU ticks")
{
    InterruptController interrupts;
    Timer timer(interrupts);

    timer.reset();

    timer.tick_dots(16384);

    REQUIRE(timer.take_div_apu_ticks() == 2);
    REQUIRE(timer.take_div_apu_ticks() == 0);
}
