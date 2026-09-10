#pragma once

#include <array>
#include <cstdint>
#include <vector>

class InterruptController;
class MemBus;

class CPU {
public:
    using DotCallback = void (*)(void* context);

    CPU(MemBus* bus, bool isTest);
    CPU(MemBus* bus, InterruptController* interrupts, bool isTest);

    void reset();
    bool step_m_cycle();
    bool step_m_cycle(DotCallback tick_dot, void* context);
    int step();

    bool instruction_boundary() const;
    bool is_halted() const;
    bool is_stopped() const;
    bool is_locked_up() const;
    bool interrupt_master_enabled() const;
    void wake_from_stop();

    void debugPrintState() const;

    uint16_t af() const;
    uint16_t bc() const;
    uint16_t de() const;
    uint16_t hl() const;

    void set_af(uint16_t value);
    void set_bc(uint16_t value);
    void set_de(uint16_t value);
    void set_hl(uint16_t value);

    bool flag_z() const;
    bool flag_n() const;
    bool flag_h() const;
    bool flag_c() const;

    // Kept public while the CPU tests directly inspect architectural state.
    uint8_t a = 0;
    uint8_t f = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    uint8_t d = 0;
    uint8_t e = 0;
    uint8_t h = 0;
    uint8_t l = 0;
    uint16_t sp = 0;
    uint16_t pc = 0;

    std::vector<uint8_t> testMemory;
    bool isTest = false;

private:
    static constexpr uint8_t flag_z_mask = 0x80;
    static constexpr uint8_t flag_n_mask = 0x40;
    static constexpr uint8_t flag_h_mask = 0x20;
    static constexpr uint8_t flag_c_mask = 0x10;
    static constexpr uint32_t stop_wakeup_clock_cycles = 1u << 17;

    MemBus* bus = nullptr;
    InterruptController* interrupts = nullptr;

    bool ime = false;
    bool ime_enable_pending = false;
    bool ime_promotion_blocked = false;
    bool halted = false;
    bool just_halted = false;
    bool stopped = false;
    bool halt_bug = false;
    bool locked_up = false;
    bool stop_wakeup_requested = false;
    bool servicing_interrupt = false;
    bool at_instruction_boundary = true;
    bool condition_met = false;

    uint8_t current_opcode = 0;
    uint8_t current_cb_opcode = 0;
    uint8_t instruction_m_cycle = 0;
    uint8_t interrupt_m_cycle = 0;
    uint8_t operand8 = 0;
    uint8_t temporary8 = 0;
    uint16_t operand16 = 0;
    uint16_t interrupt_vector = 0;
    uint32_t stop_wakeup_cycles_remaining = 0;
    bool scheduling_bus_writes = false;
    std::array<uint16_t, 2> scheduled_io_write_addresses{};
    uint8_t scheduled_io_write_count = 0;

    struct TimedWrite {
        uint16_t address = 0;
        uint8_t value = 0;
        uint8_t dots_remaining = 0;
    };
    std::array<TimedWrite, 4> timed_writes{};

    void execute_cpu_m_cycle();
    void fetch_opcode();
    void execute_fetched_opcode();
    void execute_instruction_m_cycle();
    void execute_cb_m_cycle();
    void execute_interrupt_m_cycle();
    void start_interrupt();
    void finish_instruction();
    void schedule_dmg_io_write(uint16_t addr, uint8_t value,
                               uint8_t dots_until_normal_write);
    void schedule_write(uint16_t addr, uint8_t value, uint8_t dots_until_write);
    void tick_scheduled_writes();
    void tick_cpu_dot(DotCallback tick_dot, void* context);

    bool interrupt_pending() const;
    static uint16_t vector_for_interrupt(uint8_t interrupt);
    static bool invalid_opcode(uint8_t opcode);

    uint8_t fetch8();
    uint8_t read8(uint16_t addr);
    uint8_t read8_and_internal(uint16_t addr);
    void write8(uint16_t addr, uint8_t value);
    void write8_now(uint16_t addr, uint8_t value);
    void internal_cycle(uint16_t addr);

    uint8_t read_register(int index) const;
    void write_register(int index, uint8_t value);
    uint16_t read_register_pair(int index) const;
    void write_register_pair(int index, uint16_t value);
    uint16_t read_stack_pair(int index) const;
    void write_stack_pair(int index, uint16_t value);

    bool condition(int index) const;
    void execute_alu_operation(int operation, uint8_t value);
    uint8_t execute_cb_operation(uint8_t opcode, uint8_t value);

    void set_z(bool value);
    void set_n(bool value);
    void set_h(bool value);
    void set_c(bool value);

    uint8_t add8(uint8_t lhs, uint8_t rhs, bool carry);
    uint8_t sub8(uint8_t lhs, uint8_t rhs, bool carry);
    uint8_t inc8(uint8_t value);
    uint8_t dec8(uint8_t value);
    void add_hl(uint16_t value);
    uint16_t add_sp_offset(uint8_t offset);
    void decimal_adjust_accumulator();
};
