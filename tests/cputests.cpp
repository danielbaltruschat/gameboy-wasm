#include <catch2/catch_test_macros.hpp>

#include "cpu.h"


TEST_CASE("CPU instruction LB_BC_d16(0x01) runs correctly") {
    CPU cpu(nullptr, true);

    //loads the next 2 bytes of immediate data into register B and C pair
    cpu.testMemory = { 0x01, 0x42, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00}; //should load 0x43 to reg C and 0x42 to reg B
    cpu.step();
    REQUIRE(cpu.bc() == 0x4243);
    REQUIRE(cpu.pc == 3);
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