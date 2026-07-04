#pragma once

#include <cstdint>
#include <optional>

enum class Interrupt {
    VBlank,
    LCDStat,
    Timer,
    Serial,
    Joypad
};

class InterruptController {
public:
    void reset(); // reset interrupt state and clear registers

    void request(Interrupt i); // Set corresponding bit in IF

    // Get highest priority interrupt that is requested and enabled
    std::optional<Interrupt> highest_priority_pending() const;

    void acknowledge(Interrupt i); // Clears interrupt bit in IF after CPU handles it

    uint8_t read_if() const;
    void write_if(uint8_t value);

    uint8_t read_ie() const;
    void write_ie(uint8_t value);

private:
    uint8_t IF = 0; // interrupt flags: requested/pending
    uint8_t IE = 0; // interrupt enable
    //Store IME in CPU
};
