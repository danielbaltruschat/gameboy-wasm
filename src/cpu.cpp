CPU::CPU(MemBus& bus) : bus(bus) {
    reset();
}

CPU::~CPU() {

}

CPU::reset() {
    a = 0x01;
    f = 0xb0;
    b = 0x00;
    c = 0x13;
    d = 0x00;
    e = 0xd8;
    h = 0x01;
    l = 0x4d;

    sp = 0xfffe;
    pc = 0x0100;

    interruptEnabled = false;
    interruptFlag = false;
    halted = false;
    stopped = false;
    halt_bug = false;
}

CPU::step() {

}

CPU::fetch8() {
    uint8_t value = read8(pc);
    pc++;
    return value;
}

CPU::fetch16() {
    uint16_t value = read16(pc);
    pc += 2;
    return value;
}

CPU::execute(uint8_t opcode) {
    switch (opcode) {
        case 0x00: { return 0; }
        default:
            std::cerr << "Unknown opcode: " << std::hex << (int)opcode << std::endl;
            return -1; // Unknown opcode
    }
}