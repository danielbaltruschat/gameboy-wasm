#include "oam_dma.h"

void OamDma::reset() {
    active = false;
    dma_reg = 0x00;
    source_base = 0x0000;
    index = 0;
    dot_counter = 0;
    pending_copies = 0;
}
