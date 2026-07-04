#include <cstdint>
#include "bus.h"

class CPU {
public:
    CPU(MemBus& bus);

    void reset();
    int step();                 // execute one instruction or interrupt, return cycles
    void request_stop();         // optional, depending on design

private:
    MemBus& bus;

    // 8-bit registers
    uint8_t a, f;
    uint8_t b, c;
    uint8_t d, e;
    uint8_t h, l;

    // 16-bit registers
    uint16_t sp;
    uint16_t pc;

    // CPU control state
    bool interruptEnabled; // IE flag
    bool interruptFlag; // IF flag
    bool halted;
    bool stopped;

    // Fetch/decode/execute
    uint8_t fetch8();
    uint16_t fetch16();
    int execute(uint8_t opcode);
    int execute_cb(uint8_t opcode);

    // Interrupts
    bool handle_interrupts();

    // Memory helpers
    uint8_t read8(uint16_t addr);
    void write8(uint16_t addr, uint8_t value);
    uint16_t read16(uint16_t addr);
    void write16(uint16_t addr, uint16_t value);

    // Stack helpers
    void push16(uint16_t value);
    uint16_t pop16();

    // Register pair helpers
    uint16_t af() const;
    uint16_t bc() const;
    uint16_t de() const;
    uint16_t hl() const;

    void set_af(uint16_t value);
    void set_bc(uint16_t value);
    void set_de(uint16_t value);
    void set_hl(uint16_t value);

    // Flag helpers
    bool flag_z() const;
    bool flag_n() const;
    bool flag_h() const;
    bool flag_c() const;

    void set_z(bool value);
    void set_n(bool value);
    void set_h(bool value);
    void set_c(bool value);

    // ALU helpers
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

    // Rotate/shift/bit helpers
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