#include <catch2/catch_test_macros.hpp>

#include "oam_dma.h"

TEST_CASE("OAM reset clears transfer state")
{
    OamDma dma;

    dma.reset();

    REQUIRE_FALSE(dma.is_active());
    REQUIRE_FALSE(dma.copy_pending());
    REQUIRE(dma.read_reg() == 0x00);
}
