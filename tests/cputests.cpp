#include <catch2/catch_test_macros.hpp>

#include "cpu.h"


TEST_CASE("CPU instruction LD_BC_d16(0x01) loads a little-endian immediate") {
    CPU cpu(nullptr, true);

    cpu.testMemory = { 0x01, 0x42, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00};
    cpu.step();
    REQUIRE(cpu.bc() == 0x4342);
    REQUIRE(cpu.pc == 3);
}

TEST_CASE("CPU exposes each LD BC immediate bus cycle separately") {
    CPU cpu(nullptr, true);
    cpu.testMemory = {0x01, 0x42, 0x43};

    cpu.step_m_cycle();
    REQUIRE_FALSE(cpu.instruction_boundary());
    REQUIRE(cpu.pc == 1);
    REQUIRE(cpu.bc() == 0x0000);

    cpu.step_m_cycle();
    REQUIRE_FALSE(cpu.instruction_boundary());
    REQUIRE(cpu.pc == 2);
    REQUIRE(cpu.bc() == 0x0000);

    cpu.step_m_cycle();
    REQUIRE(cpu.instruction_boundary());
    REQUIRE(cpu.pc == 3);
    REQUIRE(cpu.bc() == 0x4342);
}

TEST_CASE("CPU delays an INC at HL writeback until its third M-cycle") {
    CPU cpu(nullptr, true);
    cpu.testMemory = {0x34, 0x00, 0xFF};
    cpu.set_hl(0x0002);

    cpu.step_m_cycle();
    REQUIRE(cpu.testMemory[2] == 0xFF);

    cpu.step_m_cycle();
    REQUIRE(cpu.testMemory[2] == 0xFF);
    REQUIRE_FALSE(cpu.instruction_boundary());

    cpu.step_m_cycle();
    REQUIRE(cpu.testMemory[2] == 0x00);
    REQUIRE(cpu.instruction_boundary());
}

TEST_CASE("CPU combines the HL read and increment in LD A at HL plus") {
    CPU cpu(nullptr, true);
    cpu.testMemory = {0x2A, 0x00, 0x5A};
    cpu.set_hl(0x0002);

    cpu.step_m_cycle();
    REQUIRE_FALSE(cpu.instruction_boundary());
    REQUIRE(cpu.hl() == 0x0002);

    cpu.step_m_cycle();
    REQUIRE(cpu.instruction_boundary());
    REQUIRE(cpu.a == 0x5A);
    REQUIRE(cpu.hl() == 0x0003);
}

TEST_CASE("CPU reset enters a deterministic raw DMG power-on state") {
    CPU cpu(nullptr, true);

    REQUIRE(cpu.af() == 0x0000);
    REQUIRE(cpu.bc() == 0x0000);
    REQUIRE(cpu.de() == 0x0000);
    REQUIRE(cpu.hl() == 0x0000);
    REQUIRE(cpu.sp == 0x0000);
    REQUIRE(cpu.pc == 0x0000);
    REQUIRE(cpu.instruction_boundary());
}


TEST_CASE("CPU instruction LD_pBC_A(0x02) runs correctly") {
    CPU cpu(nullptr, true);
    cpu.a = 0xFF;
    cpu.b = 0x00;
    cpu.c = 0x02;

    //stores the content of register A in the memory location of the B and C register pair
    cpu.testMemory = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //should store 0xFF into memory address 2
    std::vector<uint8_t> answer = {0x02, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
    cpu.step();
    REQUIRE(cpu.testMemory == answer);
    REQUIRE(cpu.pc == 1);
}


TEST_CASE("CPU instruction LD_pBC_A(0x03) runs correctly") {
    CPU cpu(nullptr, true);
    cpu.a = 0xFF;
    cpu.b = 0x00;
    cpu.c = 0xFF;

    //increments the contents of register pair BC by 1
    cpu.testMemory = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; //should do as says
    cpu.step();
    REQUIRE(cpu.bc() == 0x0100);
    REQUIRE(cpu.pc == 1);
}
