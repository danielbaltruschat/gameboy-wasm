#pragma once

#include <vector>
#include <cstdint>
#include <iostream>
#include "bus.h"

class CPUInstructions;

class CPU {
    friend class CPUInstructions;

public:
    //CPU(MemBus& bus);
    CPU();

    void reset();
    void step_m_cycle();
    //executes a single instruction
    int step();
    void request_stop();

    bool instruction_boundary() const;

    void debugPrintState();

    std::vector<uint8_t> testMemory = {
    // --- Block 1: 0x00 - 0x06 (Testing BC and B) ---
    0x00,             // 0x00: NOP
    0x01, 0x00, 0xC0, // 0x01: LD BC, 0xC000 (Note: Little Endian)
    0x02,             // 0x02: LD (BC), A    (Writes A to 0xC000)
    0x03,             // 0x03: INC BC        (BC becomes 0xC001)
    0x04,             // 0x04: INC B         (BC becomes 0xC101)
    0x05,             // 0x05: DEC B         (BC becomes 0xC001)
    0x06, 0x42,       // 0x06: LD B, 0x42    (BC becomes 0x4201)

    // --- Block 2: 0x11 - 0x16 (Testing DE and D) ---
    0x11, 0x10, 0xC0, // 0x11: LD DE, 0xC010 
    0x12,             // 0x12: LD (DE), A    (Writes A to 0xC010)
    0x13,             // 0x13: INC DE        (DE becomes 0xC011)
    0x14,             // 0x14: INC D         (DE becomes 0xC111)
    0x15,             // 0x15: DEC D         (DE becomes 0xC011)
    0x16, 0x55,       // 0x16: LD D, 0x55    (DE becomes 0x5511)

    // --- Block 3: 0x21 - 0x26 (Testing HL, H, and HL increments) ---
    0x21, 0x20, 0xC0, // 0x21: LD HL, 0xC020 
    0x22,             // 0x22: LD (HL+), A   (Writes A to 0xC020, HL becomes 0xC021)
    0x23,             // 0x23: INC HL        (HL becomes 0xC022)
    0x24,             // 0x24: INC H         (HL becomes 0xC122)
    0x25,             // 0x25: DEC H         (HL becomes 0xC022)
    0x26, 0xC0,       // 0x26: LD H, 0xC0    (HL safely set to 0xC022)

    // --- Block 4: 0x31 - 0x36 (Testing SP and (HL) memory access) ---
    0x31, 0xFE, 0xDF, // 0x31: LD SP, 0xDFFE (Top of standard WRAM)
    0x32,             // 0x32: LD (HL-), A   (Writes A to 0xC022, HL becomes 0xC021)
    0x33,             // 0x33: INC SP        (SP becomes 0xDFFF)
    0x34,             // 0x34: INC (HL)      (Increments value at 0xC021)
    0x35,             // 0x35: DEC (HL)      (Decrements value at 0xC021)
    0x36, 0x77        // 0x36: LD (HL), 0x77 (Writes 0x77 to 0xC021)
};

private:
    // MemBus& bus;

    //8 bit registers
    uint8_t a, f;
    uint8_t b, c;
    uint8_t d, e;
    uint8_t h, l;

    //16 bit registers
    uint16_t sp;
    uint16_t pc;

    //CPU state flags
    bool interruptEnabled; // IE flag
    bool interruptFlag; // IF flag
    bool halted;
    bool stopped;
    bool halt_bug;               // optional but useful for accuracy
    int pending_internal_m_cycles;
    bool at_instruction_boundary;
    bool cb_prefix_active;
    uint8_t current_opcode;
    uint8_t current_cb_opcode;
    uint8_t instruction_m_cycle;

    //FDE cycle
    uint8_t fetch8();
    uint16_t fetch16();
    int execute(uint8_t opcode);
    int execute_cb(uint8_t opcode);

    //interupt handling
    bool handle_interrupts();

    //memory helpers
    uint8_t read8(uint16_t addr);
    void write8(uint16_t addr, uint8_t value);
    uint16_t read16(uint16_t addr);
    void write16(uint16_t addr, uint16_t value);

    //stack helpers
    void push16(uint16_t value);
    uint16_t pop16();

    //register pair helpers
    uint16_t af() const;
    uint16_t bc() const;
    uint16_t de() const;
    uint16_t hl() const;

    void set_af(uint16_t value);
    void set_bc(uint16_t value);
    void set_de(uint16_t value);
    void set_hl(uint16_t value);

    //flag helpers
    bool flag_z() const;
    bool flag_n() const;
    bool flag_h() const;
    bool flag_c() const;

    void set_z(bool value);
    void set_n(bool value);
    void set_h(bool value);
    void set_c(bool value);

    //ALU helpers
    uint8_t add8(uint8_t lhs, uint8_t rhs);
    uint8_t adc8(uint8_t lhs, uint8_t rhs);
    uint8_t sub8(uint8_t lhs, uint8_t rhs);
    uint8_t sbc8(uint8_t lhs, uint8_t rhs);
    uint8_t and8(uint8_t value);
    uint8_t or8(uint8_t value);
    uint8_t xor8(uint8_t value);
    uint8_t cp8(uint8_t value);

    uint8_t inc8(uint8_t value);
    uint8_t dec8(uint8_t value);

    uint16_t add16(uint16_t lhs, uint16_t rhs);

    //bit helpers
    uint8_t rlc(uint8_t value);
    uint8_t rrc(uint8_t value);
    uint8_t rl(uint8_t value);
    uint8_t rr(uint8_t value);
    uint8_t sla(uint8_t value);
    uint8_t sra(uint8_t value);
    uint8_t srl(uint8_t value);
    uint8_t swap(uint8_t value);

    void bit(int bit, uint8_t value);
    uint8_t set_bit(int bit, uint8_t value);
    uint8_t reset_bit(int bit, uint8_t value);
};
