#include "cpu.h"

#include "bus.h"
#include "interrupt_controller.h"

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>

CPU::CPU(MemBus* bus, bool isTest)
    : CPU(bus, nullptr, isTest)
{}

CPU::CPU(MemBus* bus, InterruptController* interrupts, bool isTest)
    : isTest(isTest), bus(bus), interrupts(interrupts)
{
    reset();
}

void CPU::reset()
{
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

    ime = false;
    ime_enable_pending = false;
    ime_promotion_blocked = false;
    halted = false;
    stopped = false;
    halt_bug = false;
    locked_up = false;
    stop_wakeup_requested = false;
    servicing_interrupt = false;
    at_instruction_boundary = true;
    condition_met = false;

    current_opcode = 0;
    current_cb_opcode = 0;
    instruction_m_cycle = 0;
    interrupt_m_cycle = 0;
    operand8 = 0;
    temporary8 = 0;
    operand16 = 0;
    interrupt_vector = 0;
    stop_wakeup_cycles_remaining = 0;
}

bool CPU::step_m_cycle()
{
    return step_m_cycle(nullptr, nullptr);
}

bool CPU::step_m_cycle(DotCallback tick_dot, void* context)
{
    if (stopped) {
        if (!stop_wakeup_requested) {
            return false;
        }

        if (stop_wakeup_cycles_remaining > 4) {
            stop_wakeup_cycles_remaining -= 4;
            return false;
        }

        stop_wakeup_cycles_remaining = 0;
        stop_wakeup_requested = false;
        stopped = false;
    }

    // External reads are sampled at T4R. With whole-dot peripherals, the bus
    // action therefore occurs after three dots and before the fourth.
    if (tick_dot != nullptr) {
        tick_dot(context);
        tick_dot(context);
        tick_dot(context);
    }

    execute_cpu_m_cycle();

    if (tick_dot != nullptr) {
        tick_dot(context);
    }
    return true;
}

int CPU::step()
{
    int m_cycles = 0;
    do {
        if (!step_m_cycle()) {
            return m_cycles;
        }
        ++m_cycles;
    } while (!instruction_boundary() && !locked_up);
    return m_cycles;
}

void CPU::execute_cpu_m_cycle()
{
    const bool promote_ime = ime_enable_pending;
    ime_enable_pending = false;
    ime_promotion_blocked = false;

    if (locked_up) {
        // Undefined opcodes lock the CPU core while the rest of the SoC keeps
        // receiving the system clock.
    } else if (servicing_interrupt) {
        execute_interrupt_m_cycle();
    } else {
        if (halted) {
            if (!interrupt_pending()) {
                if (promote_ime && !ime_promotion_blocked) {
                    ime = true;
                }
                return;
            }

            halted = false;
            if (ime) {
                start_interrupt();
                execute_interrupt_m_cycle();
            } else {
                fetch_opcode();
            }
        } else if (at_instruction_boundary && ime && interrupt_pending()) {
            start_interrupt();
            execute_interrupt_m_cycle();
        } else if (at_instruction_boundary) {
            fetch_opcode();
        } else {
            execute_instruction_m_cycle();
        }
    }

    if (promote_ime && !ime_promotion_blocked) {
        ime = true;
    }
}

void CPU::fetch_opcode()
{
    if (halt_bug) {
        current_opcode = read8(pc);
        halt_bug = false;
    } else {
        current_opcode = read8_and_internal(pc);
        ++pc;
    }

    instruction_m_cycle = 1;
    at_instruction_boundary = false;
    execute_fetched_opcode();
}

void CPU::execute_fetched_opcode()
{
    const uint8_t opcode = current_opcode;

    if (invalid_opcode(opcode)) {
        locked_up = true;
        return;
    }

    if ((opcode & 0xE7) == 0x20 || (opcode & 0xE7) == 0xC0 ||
        (opcode & 0xE7) == 0xC2 || (opcode & 0xE7) == 0xC4) {
        condition_met = condition((opcode >> 3) & 0x03);
    }

    if (opcode >= 0x40 && opcode <= 0x7F) {
        if (opcode == 0x76) {
            if (!ime && interrupt_pending()) {
                halt_bug = true;
            } else {
                halted = true;
            }
            finish_instruction();
            return;
        }

        const int destination = (opcode >> 3) & 0x07;
        const int source = opcode & 0x07;
        if (destination != 6 && source != 6) {
            write_register(destination, read_register(source));
            finish_instruction();
        }
        return;
    }

    if (opcode >= 0x80 && opcode <= 0xBF) {
        const int source = opcode & 0x07;
        if (source != 6) {
            execute_alu_operation((opcode >> 3) & 0x07, read_register(source));
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xC7) == 0x04) {
        const int reg = (opcode >> 3) & 0x07;
        if (reg != 6) {
            write_register(reg, inc8(read_register(reg)));
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xC7) == 0x05) {
        const int reg = (opcode >> 3) & 0x07;
        if (reg != 6) {
            write_register(reg, dec8(read_register(reg)));
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xC7) == 0x06 || (opcode & 0xCF) == 0x01 ||
        (opcode & 0xCF) == 0x03 || (opcode & 0xCF) == 0x0B ||
        (opcode & 0xCF) == 0x09) {
        return;
    }

    switch (opcode) {
    case 0x00:
        finish_instruction();
        return;
    case 0x07: {
        const bool carry = (a & 0x80) != 0;
        a = static_cast<uint8_t>((a << 1) | (carry ? 1 : 0));
        f = carry ? flag_c_mask : 0;
        finish_instruction();
        return;
    }
    case 0x0F: {
        const bool carry = (a & 0x01) != 0;
        a = static_cast<uint8_t>((a >> 1) | (carry ? 0x80 : 0));
        f = carry ? flag_c_mask : 0;
        finish_instruction();
        return;
    }
    case 0x10:
        ++pc; // STOP always consumes its ignored second byte.
        stopped = true;
        finish_instruction();
        return;
    case 0x17: {
        const bool old_carry = flag_c();
        const bool carry = (a & 0x80) != 0;
        a = static_cast<uint8_t>((a << 1) | (old_carry ? 1 : 0));
        f = carry ? flag_c_mask : 0;
        finish_instruction();
        return;
    }
    case 0x1F: {
        const bool old_carry = flag_c();
        const bool carry = (a & 0x01) != 0;
        a = static_cast<uint8_t>((a >> 1) | (old_carry ? 0x80 : 0));
        f = carry ? flag_c_mask : 0;
        finish_instruction();
        return;
    }
    case 0x27:
        decimal_adjust_accumulator();
        finish_instruction();
        return;
    case 0x2F:
        a = static_cast<uint8_t>(~a);
        f = static_cast<uint8_t>((f & (flag_z_mask | flag_c_mask)) |
                                 flag_n_mask | flag_h_mask);
        finish_instruction();
        return;
    case 0x37:
        f = static_cast<uint8_t>((f & flag_z_mask) | flag_c_mask);
        finish_instruction();
        return;
    case 0x3F:
        f = static_cast<uint8_t>((f & flag_z_mask) |
                                 (flag_c() ? 0 : flag_c_mask));
        finish_instruction();
        return;
    case 0xE9:
        pc = hl();
        finish_instruction();
        return;
    case 0xF3:
        ime = false;
        ime_enable_pending = false;
        ime_promotion_blocked = true;
        finish_instruction();
        return;
    case 0xFB:
        ime_enable_pending = true;
        finish_instruction();
        return;
    default:
        return;
    }
}

void CPU::execute_instruction_m_cycle()
{
    const uint8_t opcode = current_opcode;

    if (opcode == 0xCB) {
        execute_cb_m_cycle();
        return;
    }

    if (opcode >= 0x40 && opcode <= 0x7F) {
        const int destination = (opcode >> 3) & 0x07;
        const int source = opcode & 0x07;
        assert(destination == 6 || source == 6);
        if (destination == 6) {
            write8(hl(), read_register(source));
        } else {
            write_register(destination, read8(hl()));
        }
        finish_instruction();
        return;
    }

    if (opcode >= 0x80 && opcode <= 0xBF) {
        assert((opcode & 0x07) == 6);
        execute_alu_operation((opcode >> 3) & 0x07, read8(hl()));
        finish_instruction();
        return;
    }

    if (opcode == 0x34 || opcode == 0x35) {
        if (instruction_m_cycle == 1) {
            operand8 = read8(hl());
            operand8 = opcode == 0x34 ? inc8(operand8) : dec8(operand8);
            instruction_m_cycle = 2;
        } else {
            write8(hl(), operand8);
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xC7) == 0x06) {
        const int reg = (opcode >> 3) & 0x07;
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            if (reg == 6) {
                instruction_m_cycle = 2;
            } else {
                write_register(reg, operand8);
                finish_instruction();
            }
        } else {
            assert(reg == 6);
            write8(hl(), operand8);
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xCF) == 0x01) {
        const int pair = (opcode >> 4) & 0x03;
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle = 2;
        } else {
            operand16 |= static_cast<uint16_t>(fetch8()) << 8;
            write_register_pair(pair, operand16);
            finish_instruction();
        }
        return;
    }

    if ((opcode & 0xCF) == 0x03 || (opcode & 0xCF) == 0x0B) {
        const int pair = (opcode >> 4) & 0x03;
        const uint16_t value = read_register_pair(pair);
        internal_cycle(value);
        write_register_pair(pair, static_cast<uint16_t>(
            opcode & 0x08 ? value - 1 : value + 1
        ));
        finish_instruction();
        return;
    }

    if ((opcode & 0xCF) == 0x09) {
        internal_cycle(0x0000);
        add_hl(read_register_pair((opcode >> 4) & 0x03));
        finish_instruction();
        return;
    }

    switch (opcode) {
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
        set_hl(static_cast<uint16_t>(address + 1));
        finish_instruction();
        return;
    }
    case 0x32: {
        const uint16_t address = hl();
        write8(address, a);
        set_hl(static_cast<uint16_t>(address - 1));
        finish_instruction();
        return;
    }
    case 0x0A:
        a = read8(bc());
        finish_instruction();
        return;
    case 0x1A:
        a = read8(de());
        finish_instruction();
        return;
    case 0x2A: {
        const uint16_t address = hl();
        a = read8_and_internal(address);
        set_hl(static_cast<uint16_t>(address + 1));
        finish_instruction();
        return;
    }
    case 0x3A: {
        const uint16_t address = hl();
        a = read8_and_internal(address);
        set_hl(static_cast<uint16_t>(address - 1));
        finish_instruction();
        return;
    }
    case 0x08:
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            operand16 |= static_cast<uint16_t>(fetch8()) << 8;
            instruction_m_cycle = 3;
        } else if (instruction_m_cycle == 3) {
            write8(operand16, static_cast<uint8_t>(sp));
            ++operand16;
            instruction_m_cycle = 4;
        } else {
            write8(operand16, static_cast<uint8_t>(sp >> 8));
            finish_instruction();
        }
        return;
    case 0x18:
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38:
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            if (opcode != 0x18 && !condition_met) {
                finish_instruction();
            } else {
                instruction_m_cycle = 2;
            }
        } else {
            const uint16_t target = static_cast<uint16_t>(
                pc + static_cast<int8_t>(operand8)
            );
            internal_cycle(0x0000);
            pc = target;
            finish_instruction();
        }
        return;
    case 0xC2:
    case 0xC3:
    case 0xCA:
    case 0xD2:
    case 0xDA:
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            operand16 |= static_cast<uint16_t>(fetch8()) << 8;
            if (opcode != 0xC3 && !condition_met) {
                finish_instruction();
            } else {
                instruction_m_cycle = 3;
            }
        } else {
            internal_cycle(0x0000);
            pc = operand16;
            finish_instruction();
        }
        return;
    case 0xC4:
    case 0xCC:
    case 0xCD:
    case 0xD4:
    case 0xDC:
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            operand16 |= static_cast<uint16_t>(fetch8()) << 8;
            if (opcode != 0xCD && !condition_met) {
                finish_instruction();
            } else {
                instruction_m_cycle = 3;
            }
        } else if (instruction_m_cycle == 3) {
            internal_cycle(sp);
            --sp;
            instruction_m_cycle = 4;
        } else if (instruction_m_cycle == 4) {
            write8(sp, static_cast<uint8_t>(pc >> 8));
            --sp;
            instruction_m_cycle = 5;
        } else {
            write8(sp, static_cast<uint8_t>(pc));
            pc = operand16;
            finish_instruction();
        }
        return;
    case 0xC0:
    case 0xC8:
    case 0xD0:
    case 0xD8:
        if (instruction_m_cycle == 1) {
            internal_cycle(0x0000);
            if (!condition_met) {
                finish_instruction();
            } else {
                instruction_m_cycle = 2;
            }
        } else if (instruction_m_cycle == 2) {
            operand16 = read8_and_internal(sp++);
            instruction_m_cycle = 3;
        } else if (instruction_m_cycle == 3) {
            operand16 |= static_cast<uint16_t>(read8(sp++)) << 8;
            instruction_m_cycle = 4;
        } else {
            internal_cycle(0x0000);
            pc = operand16;
            finish_instruction();
        }
        return;
    case 0xC9:
    case 0xD9:
        if (instruction_m_cycle == 1) {
            operand16 = read8_and_internal(sp++);
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            operand16 |= static_cast<uint16_t>(read8(sp++)) << 8;
            instruction_m_cycle = 3;
        } else {
            internal_cycle(0x0000);
            pc = operand16;
            if (opcode == 0xD9) {
                ime = true;
                ime_enable_pending = false;
            }
            finish_instruction();
        }
        return;
    case 0xC1:
    case 0xD1:
    case 0xE1:
    case 0xF1:
        if (instruction_m_cycle == 1) {
            operand16 = read8_and_internal(sp++);
            instruction_m_cycle = 2;
        } else {
            operand16 |= static_cast<uint16_t>(read8(sp++)) << 8;
            write_stack_pair((opcode >> 4) & 0x03, operand16);
            finish_instruction();
        }
        return;
    case 0xC5:
    case 0xD5:
    case 0xE5:
    case 0xF5:
        if (instruction_m_cycle == 1) {
            operand16 = read_stack_pair((opcode >> 4) & 0x03);
            internal_cycle(sp);
            --sp;
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            write8(sp, static_cast<uint8_t>(operand16 >> 8));
            --sp;
            instruction_m_cycle = 3;
        } else {
            write8(sp, static_cast<uint8_t>(operand16));
            finish_instruction();
        }
        return;
    case 0xC7:
    case 0xCF:
    case 0xD7:
    case 0xDF:
    case 0xE7:
    case 0xEF:
    case 0xF7:
    case 0xFF:
        if (instruction_m_cycle == 1) {
            internal_cycle(sp);
            --sp;
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            write8(sp, static_cast<uint8_t>(pc >> 8));
            --sp;
            instruction_m_cycle = 3;
        } else {
            write8(sp, static_cast<uint8_t>(pc));
            pc = opcode & 0x38;
            finish_instruction();
        }
        return;
    case 0xC6:
    case 0xCE:
    case 0xD6:
    case 0xDE:
    case 0xE6:
    case 0xEE:
    case 0xF6:
    case 0xFE:
        execute_alu_operation((opcode >> 3) & 0x07, fetch8());
        finish_instruction();
        return;
    case 0xE0:
    case 0xF0:
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            instruction_m_cycle = 2;
        } else {
            const uint16_t address = static_cast<uint16_t>(0xFF00 | operand8);
            if (opcode == 0xE0) {
                write8(address, a);
            } else {
                a = read8(address);
            }
            finish_instruction();
        }
        return;
    case 0xE2:
        write8(static_cast<uint16_t>(0xFF00 | c), a);
        finish_instruction();
        return;
    case 0xF2:
        a = read8(static_cast<uint16_t>(0xFF00 | c));
        finish_instruction();
        return;
    case 0xEA:
    case 0xFA:
        if (instruction_m_cycle == 1) {
            operand16 = fetch8();
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            operand16 |= static_cast<uint16_t>(fetch8()) << 8;
            instruction_m_cycle = 3;
        } else {
            if (opcode == 0xEA) {
                write8(operand16, a);
            } else {
                a = read8(operand16);
            }
            finish_instruction();
        }
        return;
    case 0xE8:
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            instruction_m_cycle = 2;
        } else if (instruction_m_cycle == 2) {
            internal_cycle(0x0000);
            operand16 = add_sp_offset(operand8);
            instruction_m_cycle = 3;
        } else {
            internal_cycle(0x0000);
            sp = operand16;
            finish_instruction();
        }
        return;
    case 0xF8:
        if (instruction_m_cycle == 1) {
            operand8 = fetch8();
            instruction_m_cycle = 2;
        } else {
            internal_cycle(0x0000);
            set_hl(add_sp_offset(operand8));
            finish_instruction();
        }
        return;
    case 0xF9:
        internal_cycle(0x0000);
        sp = hl();
        finish_instruction();
        return;
    default:
        assert(false && "valid opcode missing from CPU M-cycle decoder");
        locked_up = true;
        return;
    }
}

void CPU::execute_cb_m_cycle()
{
    if (instruction_m_cycle == 1) {
        current_cb_opcode = fetch8();
        const int reg = current_cb_opcode & 0x07;
        if (reg == 6) {
            instruction_m_cycle = 2;
        } else {
            const uint8_t value = execute_cb_operation(
                current_cb_opcode,
                read_register(reg)
            );
            if ((current_cb_opcode >> 6) != 1) {
                write_register(reg, value);
            }
            finish_instruction();
        }
        return;
    }

    if (instruction_m_cycle == 2) {
        operand8 = read8(hl());
        temporary8 = execute_cb_operation(current_cb_opcode, operand8);
        if ((current_cb_opcode >> 6) == 1) {
            finish_instruction();
        } else {
            instruction_m_cycle = 3;
        }
        return;
    }

    write8(hl(), temporary8);
    finish_instruction();
}

void CPU::start_interrupt()
{
    ime = false;
    ime_enable_pending = false;
    halted = false;
    halt_bug = false;
    servicing_interrupt = true;
    at_instruction_boundary = false;
    interrupt_m_cycle = 0;
    interrupt_vector = 0;
}

void CPU::execute_interrupt_m_cycle()
{
    assert(servicing_interrupt);

    switch (interrupt_m_cycle) {
    case 0:
        internal_cycle(0x0000);
        interrupt_m_cycle = 1;
        return;
    case 1:
        internal_cycle(sp);
        --sp;
        interrupt_m_cycle = 2;
        return;
    case 2:
        write8(sp, static_cast<uint8_t>(pc >> 8));
        --sp;
        interrupt_m_cycle = 3;
        return;
    case 3: {
        if (interrupts != nullptr) {
            const auto selected = interrupts->highest_priority_pending();
            if (selected.has_value()) {
                const uint8_t interrupt = static_cast<uint8_t>(*selected);
                interrupt_vector = vector_for_interrupt(interrupt);
                interrupts->acknowledge(*selected);
            }
        }
        write8(sp, static_cast<uint8_t>(pc));
        pc = interrupt_vector;
        interrupt_m_cycle = 4;
        return;
    }
    case 4:
        internal_cycle(0x0000);
        servicing_interrupt = false;
        finish_instruction();
        return;
    default:
        assert(false);
        return;
    }
}

void CPU::finish_instruction()
{
    f &= 0xF0;
    at_instruction_boundary = true;
    instruction_m_cycle = 0;
}

bool CPU::interrupt_pending() const
{
    return interrupts != nullptr &&
           interrupts->highest_priority_pending().has_value();
}

uint16_t CPU::vector_for_interrupt(uint8_t interrupt)
{
    assert(interrupt < 5);
    return static_cast<uint16_t>(0x0040 + interrupt * 8);
}

bool CPU::invalid_opcode(uint8_t opcode)
{
    switch (opcode) {
    case 0xD3:
    case 0xDB:
    case 0xDD:
    case 0xE3:
    case 0xE4:
    case 0xEB:
    case 0xEC:
    case 0xED:
    case 0xF4:
    case 0xFC:
    case 0xFD:
        return true;
    default:
        return false;
    }
}

uint8_t CPU::fetch8()
{
    const uint8_t value = read8_and_internal(pc);
    ++pc;
    return value;
}

uint8_t CPU::read8(uint16_t addr)
{
    if (isTest) {
        assert(!testMemory.empty());
        return testMemory[addr % testMemory.size()];
    }
    assert(bus != nullptr);
    return bus->read(addr);
}

uint8_t CPU::read8_and_internal(uint16_t addr)
{
    if (isTest) {
        assert(!testMemory.empty());
        return testMemory[addr % testMemory.size()];
    }
    assert(bus != nullptr);
    return bus->read_and_internal(addr);
}

void CPU::write8(uint16_t addr, uint8_t value)
{
    if (isTest) {
        assert(!testMemory.empty());
        testMemory[addr % testMemory.size()] = value;
        return;
    }
    assert(bus != nullptr);
    bus->write(addr, value);
}

void CPU::internal_cycle(uint16_t addr)
{
    if (isTest) {
        return;
    }
    assert(bus != nullptr);
    bus->internal_cycle(addr);
}

uint8_t CPU::read_register(int index) const
{
    switch (index) {
    case 0: return b;
    case 1: return c;
    case 2: return d;
    case 3: return e;
    case 4: return h;
    case 5: return l;
    case 7: return a;
    default:
        assert(false && "(HL) is not a CPU register");
        return 0;
    }
}

void CPU::write_register(int index, uint8_t value)
{
    switch (index) {
    case 0: b = value; return;
    case 1: c = value; return;
    case 2: d = value; return;
    case 3: e = value; return;
    case 4: h = value; return;
    case 5: l = value; return;
    case 7: a = value; return;
    default:
        assert(false && "(HL) is not a CPU register");
        return;
    }
}

uint16_t CPU::read_register_pair(int index) const
{
    switch (index) {
    case 0: return bc();
    case 1: return de();
    case 2: return hl();
    case 3: return sp;
    default:
        assert(false);
        return 0;
    }
}

void CPU::write_register_pair(int index, uint16_t value)
{
    switch (index) {
    case 0: set_bc(value); return;
    case 1: set_de(value); return;
    case 2: set_hl(value); return;
    case 3: sp = value; return;
    default:
        assert(false);
        return;
    }
}

uint16_t CPU::read_stack_pair(int index) const
{
    switch (index) {
    case 0: return bc();
    case 1: return de();
    case 2: return hl();
    case 3: return af();
    default:
        assert(false);
        return 0;
    }
}

void CPU::write_stack_pair(int index, uint16_t value)
{
    switch (index) {
    case 0: set_bc(value); return;
    case 1: set_de(value); return;
    case 2: set_hl(value); return;
    case 3: set_af(value); return;
    default:
        assert(false);
        return;
    }
}

bool CPU::condition(int index) const
{
    switch (index) {
    case 0: return !flag_z();
    case 1: return flag_z();
    case 2: return !flag_c();
    case 3: return flag_c();
    default:
        assert(false);
        return false;
    }
}

void CPU::execute_alu_operation(int operation, uint8_t value)
{
    switch (operation) {
    case 0:
        a = add8(a, value, false);
        return;
    case 1:
        a = add8(a, value, flag_c());
        return;
    case 2:
        a = sub8(a, value, false);
        return;
    case 3:
        a = sub8(a, value, flag_c());
        return;
    case 4:
        a &= value;
        f = static_cast<uint8_t>((a == 0 ? flag_z_mask : 0) | flag_h_mask);
        return;
    case 5:
        a ^= value;
        f = a == 0 ? flag_z_mask : 0;
        return;
    case 6:
        a |= value;
        f = a == 0 ? flag_z_mask : 0;
        return;
    case 7:
        static_cast<void>(sub8(a, value, false));
        return;
    default:
        assert(false);
        return;
    }
}

uint8_t CPU::execute_cb_operation(uint8_t opcode, uint8_t value)
{
    const int group = opcode >> 6;
    const int operation = (opcode >> 3) & 0x07;

    if (group == 1) {
        set_z((value & (uint8_t{1} << operation)) == 0);
        set_n(false);
        set_h(true);
        return value;
    }
    if (group == 2) {
        return static_cast<uint8_t>(value & ~(uint8_t{1} << operation));
    }
    if (group == 3) {
        return static_cast<uint8_t>(value | (uint8_t{1} << operation));
    }

    bool carry = false;
    uint8_t result = value;
    switch (operation) {
    case 0:
        carry = (value & 0x80) != 0;
        result = static_cast<uint8_t>((value << 1) | (carry ? 1 : 0));
        break;
    case 1:
        carry = (value & 0x01) != 0;
        result = static_cast<uint8_t>((value >> 1) | (carry ? 0x80 : 0));
        break;
    case 2: {
        const bool old_carry = flag_c();
        carry = (value & 0x80) != 0;
        result = static_cast<uint8_t>((value << 1) | (old_carry ? 1 : 0));
        break;
    }
    case 3: {
        const bool old_carry = flag_c();
        carry = (value & 0x01) != 0;
        result = static_cast<uint8_t>((value >> 1) | (old_carry ? 0x80 : 0));
        break;
    }
    case 4:
        carry = (value & 0x80) != 0;
        result = static_cast<uint8_t>(value << 1);
        break;
    case 5:
        carry = (value & 0x01) != 0;
        result = static_cast<uint8_t>((value >> 1) | (value & 0x80));
        break;
    case 6:
        result = static_cast<uint8_t>((value << 4) | (value >> 4));
        break;
    case 7:
        carry = (value & 0x01) != 0;
        result = static_cast<uint8_t>(value >> 1);
        break;
    default:
        assert(false);
        break;
    }

    f = static_cast<uint8_t>((result == 0 ? flag_z_mask : 0) |
                             (carry ? flag_c_mask : 0));
    return result;
}

uint8_t CPU::add8(uint8_t lhs, uint8_t rhs, bool carry)
{
    const uint16_t carry_value = carry ? 1 : 0;
    const uint16_t result = static_cast<uint16_t>(lhs) + rhs + carry_value;
    f = 0;
    set_z(static_cast<uint8_t>(result) == 0);
    set_h((lhs & 0x0F) + (rhs & 0x0F) + carry_value > 0x0F);
    set_c(result > 0xFF);
    return static_cast<uint8_t>(result);
}

uint8_t CPU::sub8(uint8_t lhs, uint8_t rhs, bool carry)
{
    const uint16_t carry_value = carry ? 1 : 0;
    const uint16_t subtrahend = static_cast<uint16_t>(rhs) + carry_value;
    const uint8_t result = static_cast<uint8_t>(lhs - subtrahend);
    f = flag_n_mask;
    set_z(result == 0);
    set_h((lhs & 0x0F) < ((rhs & 0x0F) + carry_value));
    set_c(static_cast<uint16_t>(lhs) < subtrahend);
    return result;
}

uint8_t CPU::inc8(uint8_t value)
{
    const bool carry = flag_c();
    const uint8_t result = static_cast<uint8_t>(value + 1);
    f = carry ? flag_c_mask : 0;
    set_z(result == 0);
    set_h((value & 0x0F) == 0x0F);
    return result;
}

uint8_t CPU::dec8(uint8_t value)
{
    const bool carry = flag_c();
    const uint8_t result = static_cast<uint8_t>(value - 1);
    f = static_cast<uint8_t>(flag_n_mask | (carry ? flag_c_mask : 0));
    set_z(result == 0);
    set_h((value & 0x0F) == 0);
    return result;
}

void CPU::add_hl(uint16_t value)
{
    const uint16_t lhs = hl();
    const uint32_t result = static_cast<uint32_t>(lhs) + value;
    const bool zero = flag_z();
    f = zero ? flag_z_mask : 0;
    set_h((lhs & 0x0FFF) + (value & 0x0FFF) > 0x0FFF);
    set_c(result > 0xFFFF);
    set_hl(static_cast<uint16_t>(result));
}

uint16_t CPU::add_sp_offset(uint8_t offset)
{
    f = 0;
    set_h((sp & 0x000F) + (offset & 0x0F) > 0x0F);
    set_c((sp & 0x00FF) + offset > 0x00FF);
    return static_cast<uint16_t>(sp + static_cast<int8_t>(offset));
}

void CPU::decimal_adjust_accumulator()
{
    uint8_t adjustment = 0;
    bool carry = flag_c();

    if (!flag_n()) {
        if (carry || a > 0x99) {
            adjustment |= 0x60;
            carry = true;
        }
        if (flag_h() || (a & 0x0F) > 0x09) {
            adjustment |= 0x06;
        }
        a = static_cast<uint8_t>(a + adjustment);
    } else {
        if (carry) adjustment |= 0x60;
        if (flag_h()) adjustment |= 0x06;
        a = static_cast<uint8_t>(a - adjustment);
    }

    set_z(a == 0);
    set_h(false);
    set_c(carry);
}

bool CPU::instruction_boundary() const { return at_instruction_boundary; }
bool CPU::is_halted() const { return halted; }
bool CPU::is_stopped() const { return stopped; }
bool CPU::is_locked_up() const { return locked_up; }
bool CPU::interrupt_master_enabled() const { return ime; }

void CPU::wake_from_stop()
{
    if (!stopped || stop_wakeup_requested) {
        return;
    }
    stop_wakeup_requested = true;
    stop_wakeup_cycles_remaining = stop_wakeup_clock_cycles;
}

uint16_t CPU::af() const
{
    return static_cast<uint16_t>((static_cast<uint16_t>(a) << 8) | f);
}

uint16_t CPU::bc() const
{
    return static_cast<uint16_t>((static_cast<uint16_t>(b) << 8) | c);
}

uint16_t CPU::de() const
{
    return static_cast<uint16_t>((static_cast<uint16_t>(d) << 8) | e);
}

uint16_t CPU::hl() const
{
    return static_cast<uint16_t>((static_cast<uint16_t>(h) << 8) | l);
}

void CPU::set_af(uint16_t value)
{
    a = static_cast<uint8_t>(value >> 8);
    f = static_cast<uint8_t>(value & 0xF0);
}

void CPU::set_bc(uint16_t value)
{
    b = static_cast<uint8_t>(value >> 8);
    c = static_cast<uint8_t>(value);
}

void CPU::set_de(uint16_t value)
{
    d = static_cast<uint8_t>(value >> 8);
    e = static_cast<uint8_t>(value);
}

void CPU::set_hl(uint16_t value)
{
    h = static_cast<uint8_t>(value >> 8);
    l = static_cast<uint8_t>(value);
}

bool CPU::flag_z() const { return (f & flag_z_mask) != 0; }
bool CPU::flag_n() const { return (f & flag_n_mask) != 0; }
bool CPU::flag_h() const { return (f & flag_h_mask) != 0; }
bool CPU::flag_c() const { return (f & flag_c_mask) != 0; }

void CPU::set_z(bool value)
{
    f = value ? static_cast<uint8_t>(f | flag_z_mask)
              : static_cast<uint8_t>(f & ~flag_z_mask);
}

void CPU::set_n(bool value)
{
    f = value ? static_cast<uint8_t>(f | flag_n_mask)
              : static_cast<uint8_t>(f & ~flag_n_mask);
}

void CPU::set_h(bool value)
{
    f = value ? static_cast<uint8_t>(f | flag_h_mask)
              : static_cast<uint8_t>(f & ~flag_h_mask);
}

void CPU::set_c(bool value)
{
    f = value ? static_cast<uint8_t>(f | flag_c_mask)
              : static_cast<uint8_t>(f & ~flag_c_mask);
}

void CPU::debugPrintState() const
{
    std::cout << std::hex << std::setfill('0')
              << "AF: " << std::setw(4) << af() << ' '
              << "BC: " << std::setw(4) << bc() << ' '
              << "DE: " << std::setw(4) << de() << ' '
              << "HL: " << std::setw(4) << hl() << ' '
              << "SP: " << std::setw(4) << sp << ' '
              << "PC: " << std::setw(4) << pc << '\n';
}
