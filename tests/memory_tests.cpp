#include <catch2/catch_test_macros.hpp>

#include "memory.h"

TEST_CASE("Memory reset clears WRAM and HRAM")
{
    Memory memory;

    memory.write_wram(0xC000, 0x12);
    memory.write_wram(0xDFFF, 0x34);
    memory.write_hram(0xFF80, 0x56);
    memory.write_hram(0xFFFE, 0x78);

    memory.reset();

    REQUIRE(memory.read_wram(0xC000) == 0x00);
    REQUIRE(memory.read_wram(0xDFFF) == 0x00);
    REQUIRE(memory.read_hram(0xFF80) == 0x00);
    REQUIRE(memory.read_hram(0xFFFE) == 0x00);
}

TEST_CASE("Memory WRAM stores first and last addresses independently")
{
    Memory memory;

    memory.reset();
    memory.write_wram(0xC000, 0xAA);
    memory.write_wram(0xDFFF, 0x55);

    REQUIRE(memory.read_wram(0xC000) == 0xAA);
    REQUIRE(memory.read_wram(0xDFFF) == 0x55);
}

TEST_CASE("Memory WRAM stores middle addresses independently")
{
    Memory memory;

    memory.reset();
    memory.write_wram(0xC123, 0x11);
    memory.write_wram(0xD456, 0x22);

    REQUIRE(memory.read_wram(0xC123) == 0x11);
    REQUIRE(memory.read_wram(0xD456) == 0x22);
    REQUIRE(memory.read_wram(0xC124) == 0x00);
}

TEST_CASE("Memory WRAM writes can overwrite existing values")
{
    Memory memory;

    memory.reset();
    memory.write_wram(0xC010, 0x12);
    REQUIRE(memory.read_wram(0xC010) == 0x12);

    memory.write_wram(0xC010, 0xEF);
    REQUIRE(memory.read_wram(0xC010) == 0xEF);
}

TEST_CASE("Memory HRAM stores first and last addresses independently")
{
    Memory memory;

    memory.reset();
    memory.write_hram(0xFF80, 0xA5);
    memory.write_hram(0xFFFE, 0x5A);

    REQUIRE(memory.read_hram(0xFF80) == 0xA5);
    REQUIRE(memory.read_hram(0xFFFE) == 0x5A);
}

TEST_CASE("Memory HRAM stores middle addresses independently")
{
    Memory memory;

    memory.reset();
    memory.write_hram(0xFF90, 0x33);
    memory.write_hram(0xFFC0, 0x44);

    REQUIRE(memory.read_hram(0xFF90) == 0x33);
    REQUIRE(memory.read_hram(0xFFC0) == 0x44);
    REQUIRE(memory.read_hram(0xFF91) == 0x00);
}

TEST_CASE("Memory HRAM writes can overwrite existing values")
{
    Memory memory;

    memory.reset();
    memory.write_hram(0xFFAB, 0x01);
    REQUIRE(memory.read_hram(0xFFAB) == 0x01);

    memory.write_hram(0xFFAB, 0xFE);
    REQUIRE(memory.read_hram(0xFFAB) == 0xFE);
}

TEST_CASE("Memory WRAM and HRAM do not alias each other")
{
    Memory memory;

    memory.reset();
    memory.write_wram(0xC080, 0x12);
    memory.write_hram(0xFF80, 0x34);

    REQUIRE(memory.read_wram(0xC080) == 0x12);
    REQUIRE(memory.read_hram(0xFF80) == 0x34);
}

TEST_CASE("Memory reset clears overwritten values across both RAM regions")
{
    Memory memory;

    memory.reset();
    memory.write_wram(0xC000, 0xFF);
    memory.write_wram(0xD000, 0x80);
    memory.write_wram(0xDFFF, 0x01);
    memory.write_hram(0xFF80, 0x7F);
    memory.write_hram(0xFFC0, 0x40);
    memory.write_hram(0xFFFE, 0x20);

    memory.reset();

    REQUIRE(memory.read_wram(0xC000) == 0x00);
    REQUIRE(memory.read_wram(0xD000) == 0x00);
    REQUIRE(memory.read_wram(0xDFFF) == 0x00);
    REQUIRE(memory.read_hram(0xFF80) == 0x00);
    REQUIRE(memory.read_hram(0xFFC0) == 0x00);
    REQUIRE(memory.read_hram(0xFFFE) == 0x00);
}

TEST_CASE("Memory instances keep independent storage")
{
    Memory first;
    Memory second;

    first.reset();
    second.reset();

    first.write_wram(0xC000, 0x11);
    first.write_hram(0xFF80, 0x22);
    second.write_wram(0xC000, 0x33);
    second.write_hram(0xFF80, 0x44);

    REQUIRE(first.read_wram(0xC000) == 0x11);
    REQUIRE(first.read_hram(0xFF80) == 0x22);
    REQUIRE(second.read_wram(0xC000) == 0x33);
    REQUIRE(second.read_hram(0xFF80) == 0x44);
}
