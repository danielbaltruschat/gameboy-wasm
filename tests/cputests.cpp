#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "boot_rom.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

namespace {

void set_register(CPU& cpu, int index, uint8_t value)
{
    switch (index) {
    case 0: cpu.b = value; return;
    case 1: cpu.c = value; return;
    case 2: cpu.d = value; return;
    case 3: cpu.e = value; return;
    case 4: cpu.h = value; return;
    case 5: cpu.l = value; return;
    case 7: cpu.a = value; return;
    default: return;
    }
}

uint8_t get_register(const CPU& cpu, int index)
{
    switch (index) {
    case 0: return cpu.b;
    case 1: return cpu.c;
    case 2: return cpu.d;
    case 3: return cpu.e;
    case 4: return cpu.h;
    case 5: return cpu.l;
    case 7: return cpu.a;
    default: return 0;
    }
}

struct CbResult {
    uint8_t value;
    uint8_t flags;
};

CbResult reference_cb(uint8_t opcode, uint8_t value, uint8_t initial_flags)
{
    const int group = opcode >> 6;
    const int operation = (opcode >> 3) & 0x07;

    if (group == 1) {
        return {
            value,
            static_cast<uint8_t>(
                (initial_flags & 0x10) |
                0x20 |
                ((value & (uint8_t{1} << operation)) == 0 ? 0x80 : 0)
            ),
        };
    }
    if (group == 2) {
        return {
            static_cast<uint8_t>(value & ~(uint8_t{1} << operation)),
            initial_flags,
        };
    }
    if (group == 3) {
        return {
            static_cast<uint8_t>(value | (uint8_t{1} << operation)),
            initial_flags,
        };
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
    case 2:
        carry = (value & 0x80) != 0;
        result = static_cast<uint8_t>(
            (value << 1) | ((initial_flags & 0x10) != 0 ? 1 : 0)
        );
        break;
    case 3:
        carry = (value & 0x01) != 0;
        result = static_cast<uint8_t>(
            (value >> 1) | ((initial_flags & 0x10) != 0 ? 0x80 : 0)
        );
        break;
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
    }

    return {
        result,
        static_cast<uint8_t>((result == 0 ? 0x80 : 0) |
                             (carry ? 0x10 : 0)),
    };
}

struct DotObservation {
    CPU* cpu;
    std::array<uint8_t, 4> values{};
    int dots = 0;
};

void observe_write_phase(void* context)
{
    auto& observation = *static_cast<DotObservation*>(context);
    observation.values[observation.dots] = observation.cpu->testMemory[2];
    ++observation.dots;
}

struct CpuBusFixture {
    InterruptController interrupts;
    BootRom boot_rom;
    Memory memory;
    std::vector<uint8_t> rom;
    Cartridge cartridge;
    PPU ppu;
    Timer timer;
    Joypad joypad;
    MemBus bus;
    CPU cpu;

    explicit CpuBusFixture(std::vector<uint8_t> image)
        : rom(std::move(image)),
          cartridge(rom),
          ppu(interrupts),
          timer(interrupts),
          joypad(interrupts),
          bus(boot_rom, cartridge, ppu, timer, joypad, interrupts, memory),
          cpu(&bus, &interrupts, false)
    {
        interrupts.reset();
        memory.reset();
        ppu.reset();
        timer.reset();
        joypad.reset();
        bus.reset();
        cpu.reset();
    }
};

std::vector<uint8_t> make_cpu_rom()
{
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0147] = 0x00;
    rom[0x0149] = 0x00;
    return rom;
}

} // namespace


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

TEST_CASE("CPU implements every valid base opcode with its documented M-cycle count")
{
    // Conditions use F=0: NZ and NC are taken, Z and C are not taken.
    static constexpr std::array<uint8_t, 256> expected_cycles = {
        1,3,2,2,1,1,2,1,5,2,2,2,1,1,2,1,
        1,3,2,2,1,1,2,1,3,2,2,2,1,1,2,1,
        3,3,2,2,1,1,2,1,2,2,2,2,1,1,2,1,
        3,3,2,2,3,3,3,1,2,2,2,2,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        2,2,2,2,2,2,1,2,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
        5,3,4,4,6,4,2,4,2,4,3,2,3,6,2,4,
        5,3,4,0,6,4,2,4,2,4,3,0,3,0,2,4,
        3,3,2,0,0,4,2,4,4,1,4,0,0,0,2,4,
        3,3,2,1,0,4,2,4,3,2,4,1,0,0,2,4,
    };

    for (int opcode = 0; opcode <= 0xFF; ++opcode) {
        if (expected_cycles[opcode] == 0) {
            continue;
        }

        CPU cpu(nullptr, true);
        cpu.testMemory.assign(0x10000, 0);
        cpu.testMemory[0] = static_cast<uint8_t>(opcode);
        cpu.testMemory[1] = 0x34;
        cpu.testMemory[2] = 0x12;
        cpu.sp = 0x8000;
        cpu.set_hl(0x4000);
        cpu.f = 0;

        CAPTURE(opcode);
        REQUIRE(cpu.step() == expected_cycles[opcode]);
        REQUIRE(cpu.instruction_boundary());
        REQUIRE((cpu.f & 0x0F) == 0);
    }
}

TEST_CASE("CPU conditional instructions use distinct taken and untaken timings")
{
    struct TimingCase {
        uint8_t opcode;
        int cycles;
    };
    static constexpr std::array<TimingCase, 16> cases = {{
        {0x20, 2}, {0x28, 3}, {0x30, 2}, {0x38, 3},
        {0xC2, 3}, {0xCA, 4}, {0xD2, 3}, {0xDA, 4},
        {0xC4, 3}, {0xCC, 6}, {0xD4, 3}, {0xDC, 6},
        {0xC0, 2}, {0xC8, 5}, {0xD0, 2}, {0xD8, 5},
    }};

    for (const auto& test : cases) {
        CPU cpu(nullptr, true);
        cpu.testMemory.assign(0x10000, 0);
        cpu.testMemory[0] = test.opcode;
        cpu.testMemory[1] = 0x04;
        cpu.testMemory[2] = 0x00;
        cpu.sp = 0x8000;
        cpu.f = 0x90; // Z=1, C=1.

        CAPTURE(test.opcode);
        REQUIRE(cpu.step() == test.cycles);
    }
}

TEST_CASE("CPU implements all CB register and memory operations")
{
    constexpr uint8_t initial_value = 0x81;
    constexpr uint8_t initial_flags = 0x10;

    for (int opcode = 0; opcode <= 0xFF; ++opcode) {
        CPU cpu(nullptr, true);
        cpu.testMemory.assign(0x400, 0);
        cpu.testMemory[0] = 0xCB;
        cpu.testMemory[1] = static_cast<uint8_t>(opcode);
        cpu.f = initial_flags;

        const int target = opcode & 0x07;
        if (target == 6) {
            cpu.set_hl(0x0200);
            cpu.testMemory[0x0200] = initial_value;
        } else {
            set_register(cpu, target, initial_value);
        }

        const CbResult expected = reference_cb(
            static_cast<uint8_t>(opcode),
            initial_value,
            initial_flags
        );
        const int group = opcode >> 6;
        const int expected_cycles = target == 6 ? (group == 1 ? 3 : 4) : 2;

        CAPTURE(opcode);
        REQUIRE(cpu.step() == expected_cycles);
        REQUIRE(cpu.f == expected.flags);
        if (target == 6) {
            REQUIRE(cpu.testMemory[0x0200] == expected.value);
        } else {
            REQUIRE(get_register(cpu, target) == expected.value);
        }
    }
}

TEST_CASE("CPU arithmetic implements carry borrow and half-carry flags")
{
    struct AluCase {
        uint8_t opcode;
        uint8_t lhs;
        uint8_t rhs;
        uint8_t initial_flags;
        uint8_t result;
        uint8_t flags;
    };
    static constexpr std::array<AluCase, 8> cases = {{
        {0x80, 0x0F, 0x01, 0x00, 0x10, 0x20},
        {0x80, 0xFF, 0x01, 0x00, 0x00, 0xB0},
        {0x88, 0x0F, 0x00, 0x10, 0x10, 0x20},
        {0x90, 0x10, 0x01, 0x00, 0x0F, 0x60},
        {0x90, 0x00, 0x01, 0x00, 0xFF, 0x70},
        {0x98, 0x10, 0x0F, 0x10, 0x00, 0xE0},
        {0xA0, 0xF0, 0x0F, 0x00, 0x00, 0xA0},
        {0xB8, 0x42, 0x42, 0x00, 0x42, 0xC0},
    }};

    for (const auto& test : cases) {
        CPU cpu(nullptr, true);
        cpu.testMemory = {test.opcode};
        cpu.a = test.lhs;
        cpu.b = test.rhs;
        cpu.f = test.initial_flags;

        CAPTURE(test.opcode, test.lhs, test.rhs);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.a == test.result);
        REQUIRE(cpu.f == test.flags);
    }
}

TEST_CASE("CPU decimal adjust handles addition and subtraction results")
{
    struct DaaCase {
        uint8_t accumulator;
        uint8_t initial_flags;
        uint8_t result;
        uint8_t flags;
    };
    static constexpr std::array<DaaCase, 4> cases = {{
        {0x3C, 0x00, 0x42, 0x00},
        {0x21, 0x30, 0x87, 0x10},
        {0x33, 0x40, 0x33, 0x40},
        {0x0F, 0x60, 0x09, 0x40},
    }};

    for (const auto& test : cases) {
        CPU cpu(nullptr, true);
        cpu.testMemory = {0x27};
        cpu.a = test.accumulator;
        cpu.f = test.initial_flags;

        CAPTURE(test.accumulator, test.initial_flags);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.a == test.result);
        REQUIRE(cpu.f == test.flags);
    }
}

TEST_CASE("CPU CALL and RET use hardware stack byte order")
{
    CPU cpu(nullptr, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0] = 0xCD;
    cpu.testMemory[1] = 0x34;
    cpu.testMemory[2] = 0x12;
    cpu.testMemory[0x1234] = 0xC9;
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 6);
    REQUIRE(cpu.pc == 0x1234);
    REQUIRE(cpu.sp == 0x7FFE);
    REQUIRE(cpu.testMemory[0x7FFF] == 0x00);
    REQUIRE(cpu.testMemory[0x7FFE] == 0x03);

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.pc == 0x0003);
    REQUIRE(cpu.sp == 0x8000);
}

TEST_CASE("CPU signed SP additions use unsigned low-byte flag carries")
{
    CPU cpu(nullptr, true);
    cpu.testMemory = {0xE8, 0x08};
    cpu.sp = 0xFFF8;
    cpu.f = 0xF0;

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.sp == 0x0000);
    REQUIRE(cpu.f == 0x30);
}

TEST_CASE("CPU EI delay enables interrupts only after the following M-cycle")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x08);
    interrupts.write_if(0x08);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0] = 0xFB;
    cpu.testMemory[1] = 0x00;
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 1);
    REQUIRE_FALSE(cpu.interrupt_master_enabled());
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.interrupt_master_enabled());

    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.pc == 0x0058);
    REQUIRE(cpu.sp == 0x7FFE);
    REQUIRE(cpu.testMemory[0x7FFF] == 0x00);
    REQUIRE(cpu.testMemory[0x7FFE] == 0x02);
    REQUIRE((interrupts.read_if() & 0x08) == 0);
    REQUIRE_FALSE(cpu.interrupt_master_enabled());
}

TEST_CASE("CPU DI cancels an EI whose effect is still pending")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x01);
    interrupts.write_if(0x01);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory = {0xFB, 0xF3, 0x04};

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.step() == 1);
    REQUIRE_FALSE(cpu.interrupt_master_enabled());
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.b == 1);
    REQUIRE((interrupts.read_if() & 0x01) != 0);
}

TEST_CASE("CPU consecutive EI instructions dispatch after the second EI")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x04);
    interrupts.write_if(0x04);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0x01A0] = 0xFB;
    cpu.testMemory[0x01A1] = 0xFB;
    cpu.pc = 0x01A0;
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.interrupt_master_enabled());
    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.pc == 0x0050);
    REQUIRE(cpu.testMemory[0x7FFF] == 0x01);
    REQUIRE(cpu.testMemory[0x7FFE] == 0xA2);
}

TEST_CASE("CPU interrupt selection can change after the high PC push")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xFB;
    rom[0x0201] = 0x00;
    CpuBusFixture fixture(std::move(rom));
    fixture.interrupts.write_ie(0x03);
    fixture.interrupts.write_if(0x03);
    fixture.cpu.pc = 0x0200;
    fixture.cpu.sp = 0x0000;

    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 5);

    REQUIRE(fixture.cpu.pc == 0x0048);
    REQUIRE((fixture.interrupts.read_if() & 0x1F) == 0x01);
}

TEST_CASE("CPU interrupt dispatch is cancelled if the high PC push disables it")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xFB;
    rom[0x0201] = 0x00;
    CpuBusFixture fixture(std::move(rom));
    fixture.interrupts.write_ie(0x04);
    fixture.interrupts.write_if(0x04);
    fixture.cpu.pc = 0x0200;
    fixture.cpu.sp = 0x0000;

    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 5);

    REQUIRE(fixture.cpu.pc == 0x0000);
    REQUIRE(fixture.interrupts.read_ie() == 0x02);
    REQUIRE((fixture.interrupts.read_if() & 0x1F) == 0x04);
    REQUIRE_FALSE(fixture.cpu.interrupt_master_enabled());
}

TEST_CASE("CPU low PC interrupt push changes IE too late to cancel dispatch")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xFB;
    rom[0x0201] = 0x00;
    CpuBusFixture fixture(std::move(rom));
    fixture.interrupts.write_ie(0x08);
    fixture.interrupts.write_if(0x08);
    fixture.cpu.pc = 0x0200;
    fixture.cpu.sp = 0x0001;

    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 1);
    REQUIRE(fixture.cpu.step() == 5);

    REQUIRE(fixture.cpu.pc == 0x0058);
    REQUIRE(fixture.interrupts.read_ie() == 0x02);
    REQUIRE((fixture.interrupts.read_if() & 0x1F) == 0x00);
}

TEST_CASE("CPU implements the IME-zero HALT bug")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x01);
    interrupts.write_if(0x01);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory = {0x76, 0x04, 0x00};

    REQUIRE(cpu.step() == 1);
    REQUIRE_FALSE(cpu.is_halted());
    REQUIRE(cpu.pc == 1);

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.b == 1);
    REQUIRE(cpu.pc == 1);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.b == 2);
    REQUIRE(cpu.pc == 2);
}

TEST_CASE("CPU EI followed by HALT uses normal IME-one interrupt behavior")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x01);
    interrupts.write_if(0x01);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0] = 0xFB;
    cpu.testMemory[1] = 0x76;
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.interrupt_master_enabled());
    REQUIRE(cpu.step() == 5);

    REQUIRE(cpu.pc == 0x0040);
    REQUIRE(cpu.sp == 0x7FFE);
    REQUIRE(cpu.testMemory[0x7FFF] == 0x00);
    REQUIRE(cpu.testMemory[0x7FFE] == 0x02);
}

TEST_CASE("CPU begins the interrupt dummy cycle at the HALT wake boundary")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x01);
    interrupts.write_if(0x01);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0] = 0xFB; // EI
    cpu.testMemory[1] = 0x76; // HALT
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.step_m_cycle()); // HALT wake and interrupt dummy cycle.
    REQUIRE(cpu.sp == 0x8000);

    REQUIRE(cpu.step_m_cycle()); // First stack write cycle.
    REQUIRE(cpu.sp == 0x7FFF);
}

TEST_CASE("CPU begins the IME-zero resumed opcode at the HALT wake boundary")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x01);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory = {0x76, 0x04, 0x00};

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.is_halted());
    REQUIRE(cpu.step_m_cycle());
    REQUIRE(cpu.pc == 1);

    interrupts.write_if(0x01);
    REQUIRE(cpu.step() == 1);
    REQUIRE_FALSE(cpu.is_halted());
    REQUIRE(cpu.b == 1);
    REQUIRE(cpu.pc == 2);
}

TEST_CASE("CPU STOP consumes its padding byte and waits for oscillator startup")
{
    CPU cpu(nullptr, true);
    cpu.testMemory = {0x10, 0xA5, 0x04};

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.is_stopped());
    REQUIRE(cpu.pc == 2);
    REQUIRE_FALSE(cpu.step_m_cycle());

    cpu.wake_from_stop();
    for (int cycle = 0; cycle < 32767; ++cycle) {
        REQUIRE_FALSE(cpu.step_m_cycle());
    }
    REQUIRE(cpu.step_m_cycle());
    REQUIRE_FALSE(cpu.is_stopped());
    REQUIRE(cpu.b == 1);
}

TEST_CASE("CPU RETI enables immediate dispatch of another pending interrupt")
{
    InterruptController interrupts;
    interrupts.reset();
    interrupts.write_ie(0x04);
    interrupts.write_if(0x04);

    CPU cpu(nullptr, &interrupts, true);
    cpu.testMemory.assign(0x10000, 0);
    cpu.testMemory[0] = 0xD9;
    cpu.testMemory[0x8000] = 0x34;
    cpu.testMemory[0x8001] = 0x12;
    cpu.sp = 0x8000;

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.pc == 0x1234);
    REQUIRE(cpu.interrupt_master_enabled());

    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.pc == 0x0050);
    REQUIRE(cpu.sp == 0x8000);
    REQUIRE(cpu.testMemory[0x8001] == 0x12);
    REQUIRE(cpu.testMemory[0x8000] == 0x34);
    REQUIRE_FALSE(cpu.interrupt_master_enabled());
}

TEST_CASE("CPU undefined opcodes lock the core instead of acting as NOP")
{
    static constexpr std::array<uint8_t, 11> invalid = {
        0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB,
        0xEC, 0xED, 0xF4, 0xFC, 0xFD,
    };

    for (uint8_t opcode : invalid) {
        CPU cpu(nullptr, true);
        cpu.testMemory = {opcode, 0x04};

        CAPTURE(opcode);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.is_locked_up());
        REQUIRE_FALSE(cpu.instruction_boundary());
        REQUIRE(cpu.pc == 1);
        REQUIRE(cpu.step_m_cycle());
        REQUIRE(cpu.pc == 1);
        REQUIRE(cpu.b == 0);
    }
}

TEST_CASE("CPU applies a bus action after advancing the M-cycle dots")
{
    CPU cpu(nullptr, true);
    cpu.testMemory = {0x02, 0x00, 0x00};
    cpu.a = 0xA5;
    cpu.set_bc(0x0002);

    REQUIRE(cpu.step_m_cycle()); // Opcode fetch.

    DotObservation observation{&cpu};
    REQUIRE(cpu.step_m_cycle(observe_write_phase, &observation));
    REQUIRE(observation.values == std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0x00});
}

TEST_CASE("CPU commits an IF write one dot after its bus phase")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xE0; // LDH (a8),A
    rom[0x0201] = 0x0F;
    CpuBusFixture fixture(std::move(rom));
    fixture.cpu.pc = 0x0200;
    fixture.cpu.a = 0x00;
    fixture.interrupts.write_if(0x1F);

    REQUIRE(fixture.cpu.step_m_cycle()); // Opcode fetch.
    REQUIRE(fixture.cpu.step_m_cycle()); // Immediate address fetch.

    struct IfObservation {
        InterruptController& interrupts;
        std::array<uint8_t, 4> values{};
        int dots = 0;
    } observation{fixture.interrupts};

    const auto observe_if = [](void* context) {
        auto& observation = *static_cast<IfObservation*>(context);
        observation.values[observation.dots++] = observation.interrupts.read_if();
    };

    REQUIRE(fixture.cpu.step_m_cycle(observe_if, &observation));
    REQUIRE(observation.values == std::array<uint8_t, 4>{0xFF, 0xFF, 0xFF, 0xFF});

    observation.dots = 0;
    REQUIRE(fixture.cpu.step_m_cycle(observe_if, &observation));
    REQUIRE(observation.values == std::array<uint8_t, 4>{0xFF, 0xE0, 0xE0, 0xE0});
}

TEST_CASE("CPU schedules DMG palette writes around the normal write phase")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xE0; // LDH (a8),A
    rom[0x0201] = 0x47;
    CpuBusFixture fixture(std::move(rom));
    fixture.cpu.pc = 0x0200;
    fixture.cpu.a = 0xA0;
    fixture.ppu.write(0xFF47, 0x12);

    const auto tick = [](void*) {};
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // Opcode fetch.
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // Immediate address fetch.

    struct PaletteObservation {
        PPU& ppu;
        std::array<uint8_t, 4> values{};
        int dots = 0;
    } observation{fixture.ppu};

    const auto observe_palette = [](void* context) {
        auto& observation = *static_cast<PaletteObservation*>(context);
        observation.values[observation.dots++] = observation.ppu.read(0xFF47);
    };

    REQUIRE(fixture.cpu.step_m_cycle(observe_palette, &observation));
    REQUIRE(observation.values == std::array<uint8_t, 4>{0x12, 0x12, 0xB2, 0xA0});
}

TEST_CASE("CPU schedules an HL store to SCX before its normal write phase")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0x77; // LD (HL),A
    CpuBusFixture fixture(std::move(rom));
    fixture.cpu.pc = 0x0200;
    fixture.cpu.set_hl(0xFF43);
    fixture.cpu.a = 0x5A;

    const auto tick = [](void*) {};
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // Opcode fetch.

    struct ScxObservation {
        PPU& ppu;
        std::array<uint8_t, 4> values{};
        int dots = 0;
    } observation{fixture.ppu};

    const auto observe_scx = [](void* context) {
        auto& observation = *static_cast<ScxObservation*>(context);
        observation.values[observation.dots++] = observation.ppu.read(0xFF43);
    };

    REQUIRE(fixture.cpu.step_m_cycle(observe_scx, &observation));
    REQUIRE(observation.values == std::array<uint8_t, 4>{0x00, 0x00, 0x5A, 0x5A});
}

TEST_CASE("CPU exposes the DMG STAT write glitch for one dot")
{
    auto rom = make_cpu_rom();
    rom[0x0200] = 0xE0; // LDH (a8),A
    rom[0x0201] = 0x41;
    CpuBusFixture fixture(std::move(rom));
    fixture.cpu.pc = 0x0200;
    fixture.cpu.a = 0x00;
    fixture.ppu.write(0xFF40, 0x80);

    const auto tick = [](void*) {};
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // Opcode fetch.
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // Immediate address fetch.
    REQUIRE(fixture.cpu.step_m_cycle(tick, nullptr)); // STAT write.
    REQUIRE((fixture.ppu.read(0xFF41) & 0x78) == 0x78);

    fixture.ppu.tick_dots(1);
    REQUIRE((fixture.ppu.read(0xFF41) & 0x78) == 0x00);
}
