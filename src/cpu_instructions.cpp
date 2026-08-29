#include "cpu_instructions.h"
#include "cpu.h"


void CPUInstructions::NOP(CPU&) {
    //do nothing
}










void CPUInstructions::LD_BC_d16(CPU& cpu) {
    cpu.set_bc(cpu.fetch16());
}

void CPUInstructions::LD_DE_d16(CPU& cpu) {
    cpu.set_de(cpu.fetch16());
}

void CPUInstructions::LD_HL_d16(CPU& cpu) {
    cpu.set_hl(cpu.fetch16());
}

void CPUInstructions::LD_SP_d16(CPU& cpu) {
    cpu.sp = cpu.fetch16();
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

void CPUInstructions::INC_D(CPU& cpu) {
    cpu.d = cpu.inc8(cpu.d);
}

void CPUInstructions::INC_H(CPU& cpu) {
    cpu.h = cpu.inc8(cpu.h);
}

void CPUInstructions::INC_pHL(CPU& cpu) {
    uint16_t value = cpu.read8(cpu.hl());
    value = cpu.inc8(value);
    cpu.write8(cpu.hl(), value);
}






void CPUInstructions::DEC_B(CPU& cpu) {
    cpu.b = cpu.dec8(cpu.b);
}

void CPUInstructions::DEC_D(CPU& cpu) {
    cpu.d = cpu.dec8(cpu.d);
}

void CPUInstructions::DEC_H(CPU& cpu) {
    cpu.h = cpu.dec8(cpu.h);
}

void CPUInstructions::DEC_pHL(CPU& cpu) {
    uint16_t value = cpu.read8(cpu.hl());
    value = cpu.dec8(value);
    cpu.write8(cpu.hl(), value);
}










void CPUInstructions::LD_B_d8(CPU& cpu) {
    cpu.b = cpu.fetch8();
}

void CPUInstructions::LD_D_d8(CPU& cpu) {
    cpu.d = cpu.fetch8();
}

void CPUInstructions::LD_H_d8(CPU& cpu) {
    cpu.h = cpu.fetch8();
}

void CPUInstructions::LD_pHL_d8(CPU& cpu) {
    cpu.write8(cpu.hl(), cpu.fetch8());
}
