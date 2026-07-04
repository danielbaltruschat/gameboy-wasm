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
    void request(Interrupt i);

    std::optional<Interrupt> highest_priority_pending() const;

    void acknowledge(Interrupt i);

private:
    uint8_t IF = 0; // interrupt flags: requested/pending
    uint8_t IE = 0; // interrupt enable
    //Store IME in CPU
};
