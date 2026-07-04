#include "cpu_instructions.h"
#include "cpu.h"


void CPUInstructions::NOP(CPU&) {
    //do nothing
}










void CPUInstructions::LD_BC_d16(CPU& cpu, uint16_t addr) {
    cpu.set_bc(addr);
}

void CPUInstructions::LD_DE_d16(CPU& cpu, uint16_t addr) {
    cpu.set_de(addr);
}

void CPUInstructions::LD_HL_d16(CPU& cpu, uint16_t addr) {
    cpu.set_hl(addr);
}

void CPUInstructions::LD_SP_d16(CPU& cpu, uint16_t addr) {
    cpu.sp = addr;
}











void CPUInstructions::LD_pBC_A(CPU& cpu) {
    cpu.write8(cpu.bc(), cpu.a);
}

void CPUInstructions::LD_pDE_A(CPU& cpu) {
    cpu.write8(cpu.de(), cpu.a);
}

void CPUInstructions::LD_pHLp_A(CPU& cpu) {
    cpu.write8(cpu.hl(), cpu.a);
    cpu.set_hl(cpu.hl() + 1);
}

void CPUInstructions::LD_pHLm_A(CPU& cpu) {
    cpu.write8(cpu.hl(), cpu.a);
    cpu.set_hl(cpu.hl() - 1);
}









void CPUInstructions::INC_BC(CPU& cpu) {
    cpu.set_bc(cpu.bc() + 1);
}

void CPUInstructions::INC_DE(CPU& cpu) {
    cpu.set_de(cpu.de() + 1);
}

void CPUInstructions::INC_HL(CPU& cpu) {
    cpu.set_hl(cpu.hl() + 1);
}

void CPUInstructions::INC_SP(CPU& cpu) {
    cpu.sp += 1;
}

void CPUInstructions::INC_B(CPU& cpu) {
    cpu.b = cpu.inc8(cpu.b);
}