#include "cpu.h"
#include "cpu_instructions.h"
#include "bus.h"

#include <cassert>





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
    a = 0;
    f = 0;
    b = 0;
    c = 0;
    d = 0;
    e = 0;
    h = 0;
    l = 0;

    sp = 0;
    pc = 0;

    interruptEnabled = false;
    interruptFlag = false;
    halted = false;
    stopped = false;
    halt_bug = false;
    pending_internal_m_cycles = 0;
    at_instruction_boundary = true;
    cb_prefix_active = false;
    current_opcode = 0;
    current_cb_opcode = 0;
    instruction_m_cycle = 0;
    operand8 = 0;
    operand16 = 0;
}

int CPU::step() {
    int cycles = 0;

    do {
        step_m_cycle();
        cycles++;
    } while (!instruction_boundary());

    return cycles;
}

void CPU::step_m_cycle() {
    if (at_instruction_boundary) {
        current_opcode = fetch8();
        instruction_m_cycle = 1;
        at_instruction_boundary = false;

        switch (current_opcode) {
        case 0x00:
            finish_instruction();
            break;
        case 0x04:
            b = inc8(b);
            finish_instruction();
            break;
        case 0x05:
            b = dec8(b);
            finish_instruction();
            break;
        case 0x14:
            d = inc8(d);
            finish_instruction();
            break;
        case 0x15:
            d = dec8(d);
            finish_instruction();
            break;
        case 0x24:
            h = inc8(h);
            finish_instruction();
            break;
        case 0x25:
            h = dec8(h);
            finish_instruction();
            break;
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x06:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x16:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x26:
        case 0x2A:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x3A:
            break;
        default:
            finish_instruction();
            break;
        }
        return;
    }

    switch (current_opcode) {
    case 0x01:
    case 0x11:
    case 0x21:
    case 0x31:
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle++;
            return;
        }
        operand16 |= static_cast<uint16_t>(fetch8()) << 8;
        if (current_opcode == 0x01) set_bc(operand16);
        if (current_opcode == 0x11) set_de(operand16);
        if (current_opcode == 0x21) set_hl(operand16);
        if (current_opcode == 0x31) sp = operand16;
        finish_instruction();
        return;
    case 0x02:
        write8(bc(), a);
        finish_instruction();
        return;
    case 0x12:
        write8(de(), a);
        finish_instruction();
        return;
    case 0x22: {
        const uint16_t address = hl();
        write8(address, a);
        set_hl(address + 1);
        finish_instruction();
        return;
    }
    case 0x32: {
        const uint16_t address = hl();
        write8(address, a);
        set_hl(address - 1);
        finish_instruction();
        return;
    }
    case 0x2A: {
        const uint16_t address = hl();
        a = read8_and_internal(address);
        set_hl(address + 1);
        finish_instruction();
        return;
    }
    case 0x3A: {
        const uint16_t address = hl();
        a = read8_and_internal(address);
        set_hl(address - 1);
        finish_instruction();
        return;
    }
    case 0x03: {
        const uint16_t address = bc();
        internal_cycle(address);
        set_bc(address + 1);
        finish_instruction();
        return;
    }
    case 0x13: {
        const uint16_t address = de();
        internal_cycle(address);
        set_de(address + 1);
        finish_instruction();
        return;
    }
    case 0x23: {
        const uint16_t address = hl();
        internal_cycle(address);
        set_hl(address + 1);
        finish_instruction();
        return;
    }
    case 0x33:
        internal_cycle(sp);
        sp++;
        finish_instruction();
        return;
    case 0x06:
        b = fetch8();
        finish_instruction();
        return;
    case 0x16:
        d = fetch8();
        finish_instruction();
        return;
    case 0x26:
        h = fetch8();
        finish_instruction();
        return;
    case 0x34:
    case 0x35:
        if (instruction_m_cycle == 1) {
            operand8 = read8(hl());
            operand8 = current_opcode == 0x34 ? inc8(operand8) : dec8(operand8);
            instruction_m_cycle++;
            return;
        }
        write8(hl(), operand8);
        finish_instruction();
        return;
    case 0x36:
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            instruction_m_cycle++;
            return;
        }
        write8(hl(), operand8);
        finish_instruction();
        return;
    default:
        finish_instruction();
        return;
    }
}

bool CPU::instruction_boundary() const {
    return at_instruction_boundary;
}

void CPU::finish_instruction() {
    at_instruction_boundary = true;
    instruction_m_cycle = 0;
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
    if (isTest) return testMemory[addr % testMemory.size()];
    assert(bus != nullptr);
    return bus->read(addr);
}

uint8_t CPU::read8_and_internal(uint16_t addr) {
    if (isTest) return testMemory[addr % testMemory.size()];
    assert(bus != nullptr);
    return bus->read_and_internal(addr);
}

void CPU::write8(uint16_t addr, uint8_t value) {
    if (isTest) {
        testMemory[addr % testMemory.size()] = value;
        return;
    }
    assert(bus != nullptr);
    bus->write(addr, value);
}

void CPU::internal_cycle(uint16_t addr) {
    if (isTest) return;
    assert(bus != nullptr);
    bus->internal_cycle(addr);
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
