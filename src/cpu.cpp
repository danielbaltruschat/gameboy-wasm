#include "cpu.h"

CPU::CPU(MemBus& bus) : bus(bus) {
    reset();
}

void CPU::reset() {
    a = 0x01;
    f = 0xb0;
    b = 0x00;
    c = 0x13;
    d = 0x00;
    e = 0xd8;
    h = 0x01;
    l = 0x4d;

    sp = 0xfffe;
    pc = 0x0100;

    interruptEnabled = false;
    interruptFlag = false;
    halted = false;
    stopped = false;
}

int CPU::step() {
    return 0;
}

uint8_t CPU::fetch8() {
    uint8_t value = read8(pc);
    pc++;
    return value;
}

uint16_t CPU::fetch16() {
    uint16_t value = read16(pc);
    pc += 2;
    return value;
}

int CPU::execute(uint8_t opcode) {
    switch (opcode) {
        case 0x00: { return 0; }
        default: return -1;
    }
}