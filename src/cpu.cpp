#include "cpu.h"
#include "cpu_instructions.h"
#include "bus.h"






int CPU::execute(uint8_t opcode) {
    switch (opcode) {
        case 0x00: { CPUInstructions::NOP(*this); return 1; }           case 0x01: { CPUInstructions::LD_BC_d16(*this); return 3; }          case 0x02: { CPUInstructions::LD_pBC_A(*this); return 2; }          case 0x03: { CPUInstructions::INC_BC(*this); return 2; }            case 0x04: { CPUInstructions::INC_B(*this); return 1; }         case 0x05: { CPUInstructions::DEC_B(*this); return 1; }            case 0x06: { CPUInstructions::LD_B_d8(*this); return 2; }
                                                                        case 0x11: { CPUInstructions::LD_DE_d16(*this); return 3; }          case 0x12: { CPUInstructions::LD_pDE_A(*this); return 2; }          case 0x13: { CPUInstructions::INC_DE(*this); return 2; }            case 0x14: { CPUInstructions::INC_D(*this); return 1; }         case 0x15: { CPUInstructions::DEC_D(*this); return 1; }            case 0x16: { CPUInstructions::LD_D_d8(*this); return 2; }
                                                                        case 0x21: { CPUInstructions::LD_HL_d16(*this); return 3; }          case 0x22: { CPUInstructions::LD_pHLp_A(*this); return 2; }         case 0x23: { CPUInstructions::INC_HL(*this); return 2; }            case 0x24: { CPUInstructions::INC_H(*this); return 1; }         case 0x25: { CPUInstructions::DEC_H(*this); return 1; }            case 0x26: { CPUInstructions::LD_H_d8(*this); return 2; }
                                                                        case 0x31: { CPUInstructions::LD_SP_d16(*this); return 3; }          case 0x32: { CPUInstructions::LD_pHLm_A(*this); return 2; }         case 0x33: { CPUInstructions::INC_SP(*this); return 2; }            case 0x34: { CPUInstructions::INC_pHL(*this); return 3; }       case 0x35: { CPUInstructions::DEC_pHL(*this); return 3; }          case 0x36: { CPUInstructions::LD_pHL_d8(*this); return 3; }
        default: return -1;
    }
}

CPU::CPU(MemBus* bus, bool isTest) : bus(bus), isTest(isTest) {
    reset();
}

void CPU::debugPrintState() {
    std::cout << "A: " << std::hex << (int)a << " F: " << std::hex << (int)f << std::endl;
    std::cout << "B: " << std::hex << (int)b << " C: " << std::hex << (int)c << std::endl;
    std::cout << "D: " << std::hex << (int)d << " E: " << std::hex << (int)e << std::endl;
    std::cout << "H: " << std::hex << (int)h << " L: " << std::hex << (int)l << std::endl;
    std::cout << "SP: " << std::hex << sp << " PC: " << std::hex << pc << std::endl;
    std::cout << "memory: ";
    for (size_t i = 0; i < testMemory.size(); ++i) {
        std::cout << std::hex << (int)testMemory[i] << " ";
    }
    std::cout << std::endl;
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
    pc = 0x0000; // 0x0100

    interruptEnabled = false;
    interruptFlag = false;
    halted = false;
    stopped = false;
}

int CPU::step() {
    uint8_t opcode = fetch8();
    int cycles = execute(opcode);
    return cycles;
}

uint8_t CPU::fetch8() {
    uint8_t value = read8(pc);
    pc += 1;
    return value;
}

uint16_t CPU::fetch16() {
    uint16_t value = read16(pc);
    pc += 2;
    return value;
}

uint8_t CPU::read8(uint16_t addr) {
    if(isTest) return testMemory[addr % testMemory.size()];
    //return bus.read(addr);
    return 0x00;
}

void CPU::write8(uint16_t addr, uint8_t value) {
    if(isTest) testMemory[addr % testMemory.size()] = value;
    //bus.write(addr, value);
}

uint16_t CPU::read16(uint16_t addr) {
    uint8_t low = read8(addr);
    uint8_t high = read8(addr + 1);
    return (high << 8) | low;
}

void CPU::write16(uint16_t addr, uint16_t value) {
    write8(addr, value & 0xFF);
    write8(addr + 1, (value >> 8) & 0xFF);
}

uint16_t CPU::af() const {
    return (a << 8) | f;
}

uint16_t CPU::bc() const {
    return (b << 8) | c;
}

uint16_t CPU::de() const {
    return (d << 8) | e;
}

uint16_t CPU::hl() const {
    return (h << 8) | l;
}

void CPU::set_af(uint16_t value) {
    a = value >> 8;
    f = value & 0xF0;
}

void CPU::set_bc(uint16_t value) {
    b = value >> 8;
    c = value & 0xFF;
}

void CPU::set_de(uint16_t value) {
    d = value >> 8;
    e = value & 0xFF;
}

void CPU::set_hl(uint16_t value) {
    h = value >> 8;
    l = value & 0xFF;
}

void CPU::set_z(bool value) {
    if (value) {
        f |= 0x80;
    } else {
        f &= ~0x80;
    }
}

void CPU::set_n(bool value) {
    if (value) {
        f |= 0x40;
    } else {
        f &= ~0x40;
    }
}

void CPU::set_h(bool value) {
    if (value) {
        f |= 0x20;
    } else {
        f &= ~0x20;
    }
}

void CPU::set_c(bool value) {
    if (value) {
        f |= 0x10;
    } else {
        f &= ~0x10;
    }
}

uint8_t CPU::inc8(uint8_t value) {
    uint8_t result = value + 1;
    set_z(result == 0);
    set_n(false);
    set_h((value & 0x0F) == 0x0F);
    return result;
}

uint8_t CPU::dec8(uint8_t value) {
    uint8_t result = value - 1;
    set_z(result == 0);
    set_n(true);
    set_h((value & 0x0F) == 0x00);
    return result;
}
