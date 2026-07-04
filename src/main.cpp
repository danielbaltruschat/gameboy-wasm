#include <iostream>
#include "cpu.h"


int main() {
    CPU cpu;
    for (int i = 0; i < 3; ++i) {
        cpu.step();
    }
    cpu.debugPrintState();
    return 0;
}
