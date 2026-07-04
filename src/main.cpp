#include <iostream>
#include "cpu.h"


int main() {
    CPU cpu;
    for (int i = 0; i < cpu.testMemory.size(); ++i) {
        cpu.step();
    }
    cpu.debugPrintState();
    return 0;
}
