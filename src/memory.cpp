#include "memory.h"

//FIXME - not currently hardware accurate and should technically be randomised with a hardware pattern model
void Memory::reset() {
    for (int i = 0; i < 0x2000; i++) {
        wram[i] = 0x00;
    }
    for (int i = 0; i < 0x7F; i++) {
        hram[i] = 0x00;
    }
}

uint8_t Memory::read_wram(uint16_t addr) const {
    return wram[addr - 0xC000];
}

void Memory::write_wram(uint16_t addr, uint8_t value) {
    wram[addr - 0xC000] = value;
}

uint8_t Memory::read_hram(uint16_t addr) const {
    return hram[addr - 0xFF80];
}

void Memory::write_hram(uint16_t addr, uint8_t value) {
    hram[addr - 0xFF80] = value;
}
