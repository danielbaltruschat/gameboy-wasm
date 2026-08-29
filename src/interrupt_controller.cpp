#include "interrupt_controller.h"

void InterruptController::reset() {
    IF = 0x00;
    IE = 0x00;
}

uint8_t InterruptController::read_if() const { return IF | 0xE0; } // Bits 5-7 read back as 1

void InterruptController::write_if(uint8_t value) { IF = value & 0x1F; } // Only lower 5 bits matter

uint8_t InterruptController::read_ie() const { return IE; }

void InterruptController::write_ie(uint8_t value) { IE = value; }

void InterruptController::request(Interrupt i) {
    //set bit in IF
    IF = IF | (1 << static_cast<uint8_t>(i));
}

void InterruptController::acknowledge(Interrupt i) {
    IF &= static_cast<uint8_t>(~(1 << static_cast<uint8_t>(i)));

}

std::optional<Interrupt> InterruptController::highest_priority_pending() const {
    uint8_t pending = IF & IE & 0x1F;

    if (pending & 0x01) return Interrupt::VBlank;
    if (pending & 0x02) return Interrupt::LCDStat;
    if (pending & 0x04) return Interrupt::Timer;
    if (pending & 0x08) return Interrupt::Serial;
    if (pending & 0x10) return Interrupt::Joypad;

    return std::nullopt;
}
