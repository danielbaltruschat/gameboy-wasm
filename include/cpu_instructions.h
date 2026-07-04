#include <cstdint>

class cpu_instructions {
    public:
        // 8-bit instructions
        // 0x00                 // 0x01                  // 0x02                  // 0x03               // 0x04                 // 0x05                 // 0x06                  // 0x07               // 0x08                   // 0x09                  // 0x0A                  // 0x0B               // 0x0C                 // 0x0D                 // 0x0E                  // 0x0F
        static void NOP();      static void LD_BC_d16(); static void LD_pBC_A();  static void INC_BC(); static void INC_B();    static void DEC_B();    static void LD_B_d8();   static void RLCA();   static void LD_pa16_SP(); static void ADD_HL_BC(); static void LD_A_pBC();  static void DEC_BC(); static void INC_C();    static void DEC_C();    static void LD_C_d8();   static void RRCA();    // 0x
        static void STOP_0();   static void LD_DE_d16(); static void LD_pDE_A();  static void INC_DE(); static void INC_D();    static void DEC_D();    static void LD_D_d8();   static void RLA();    static void JR_r8();      static void ADD_HL_DE(); static void LD_A_pDE();  static void DEC_DE(); static void INC_E();    static void DEC_E();    static void LD_E_d8();   static void RRA();     // 1x
        static void JR_NZ_s8(); static void LD_HL_d16(); static void LD_pHLp_A(); static void INC_HL(); static void INC_H();    static void DEC_H();    static void LD_H_d8();   static void DAA();    static void JR_Z_s8();    static void ADD_HL_HL(); static void LD_A_pHLp(); static void DEC_HL(); static void INC_L();    static void DEC_L();    static void LD_L_d8();   static void CPL();     // 2x
        static void JR_NC_s8(); static void LD_SP_d16(); static void LD_pHLm_A(); static void INC_SP(); static void INC_pHL();  static void DEC_pHL();  static void LD_pHL_d8(); static void SCF();    static void JR_C_s8();    static void ADD_HL_SP(); static void LD_A_pHLm(); static void DEC_SP(); static void INC_A();    static void DEC_A();    static void LD_A_d8();   static void CCF();     // 3x
        static void LD_B_B();   static void LD_B_C();    static void LD_B_D();    static void LD_B_E(); static void LD_B_H();   static void LD_B_L();   static void LD_B_pHL();  static void LD_B_A(); static void LD_C_B();     static void LD_C_C();    static void LD_C_D();    static void LD_C_E(); static void LD_C_H();   static void LD_C_L();   static void LD_C_pHL();  static void LD_C_A();  // 4x





        
        // 16-bit instructions (0xCB prefix)
};