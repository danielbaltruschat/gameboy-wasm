#include <catch2/catch_test_macros.hpp>

#include "oam_dma.h"

TEST_CASE("OAM reset clears transfer state")
{
    OamDma dma;

    dma.reset();

    REQUIRE_FALSE(dma.is_active());
    REQUIRE_FALSE(dma.copy_pending());
    REQUIRE(dma.pending_copy_count() == 0);
    REQUIRE(dma.read_reg() == 0x00);
    REQUIRE(dma.source_addr() == 0x0000);
    REQUIRE(dma.oam_offset() == 0);
}

TEST_CASE("OAM DMA start waits one M-cycle before entering active state")
{
    OamDma dma;

    dma.start(0xC0);

    REQUIRE_FALSE(dma.is_active());
    REQUIRE(dma.read_reg() == 0xC0);

    dma.tick_dots(3);
    REQUIRE_FALSE(dma.is_active());

    dma.tick_dots(1);
    REQUIRE(dma.is_active());
    REQUIRE(dma.source_addr() == 0xC000);
    REQUIRE(dma.oam_offset() == 0);
    REQUIRE_FALSE(dma.copy_pending());
    REQUIRE(dma.pending_copy_count() == 0);
}

TEST_CASE("OAM DMA owns only its source bus after the warmup M-cycle")
{
    OamDma dma;

    dma.start(0xC0);
    dma.tick_dots(4);

    REQUIRE_FALSE(dma.blocks_cpu_access(0x0000));

    dma.tick_dots(4);
    dma.acknowledge_copy();

    REQUIRE(dma.blocks_cpu_access(0x0000));
    REQUIRE_FALSE(dma.blocks_cpu_access(0x8000));
    REQUIRE(dma.blocks_cpu_access(0xC000));
    REQUIRE(dma.blocks_cpu_access(0xFE00));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFF00));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFFFF));

    REQUIRE_FALSE(dma.blocks_cpu_access(0xFF80));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFFFE));
}

TEST_CASE("OAM DMA from VRAM leaves the external and IO buses available")
{
    OamDma dma;

    dma.start(0x80);
    dma.tick_dots(8);
    dma.acknowledge_copy();

    REQUIRE_FALSE(dma.blocks_cpu_access(0x0000));
    REQUIRE(dma.blocks_cpu_access(0x8000));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xC000));
    REQUIRE(dma.blocks_cpu_access(0xFE00));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFF00));
}

TEST_CASE("OAM DMA does not block CPU access when inactive")
{
    OamDma dma;

    dma.reset();

    REQUIRE_FALSE(dma.blocks_cpu_access(0x0000));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFE00));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xFFFF));
}

TEST_CASE("OAM DMA schedules one copy every four dots")
{
    OamDma dma;

    dma.start(0x80);
    dma.tick_dots(4);

    dma.tick_dots(3);
    REQUIRE_FALSE(dma.copy_pending());
    REQUIRE(dma.pending_copy_count() == 0);

    dma.tick_dots(1);
    REQUIRE(dma.copy_pending());
    REQUIRE(dma.pending_copy_count() == 1);
    REQUIRE(dma.source_addr() == 0x8000);
    REQUIRE(dma.oam_offset() == 0);
}

TEST_CASE("OAM DMA carries partial dots between ticks")
{
    OamDma dma;

    dma.start(0x90);
    dma.tick_dots(4);

    dma.tick_dots(2);
    dma.tick_dots(2);

    REQUIRE(dma.pending_copy_count() == 1);
}

TEST_CASE("OAM DMA can schedule multiple copies from one tick")
{
    OamDma dma;

    dma.start(0xA0);
    dma.tick_dots(4);
    dma.tick_dots(12);

    REQUIRE(dma.pending_copy_count() == 3);
    REQUIRE(dma.source_addr() == 0xA000);
    REQUIRE(dma.oam_offset() == 0);
}

TEST_CASE("OAM DMA acknowledge advances source and OAM offset")
{
    OamDma dma;

    dma.start(0xC0);
    dma.tick_dots(4);
    dma.tick_dots(8);

    REQUIRE(dma.pending_copy_count() == 2);
    REQUIRE(dma.source_addr() == 0xC000);
    REQUIRE(dma.oam_offset() == 0);

    dma.acknowledge_copy();

    REQUIRE(dma.is_active());
    REQUIRE(dma.pending_copy_count() == 1);
    REQUIRE(dma.source_addr() == 0xC001);
    REQUIRE(dma.oam_offset() == 1);
}

TEST_CASE("OAM DMA acknowledge without pending copy is a no-op")
{
    OamDma dma;

    dma.start(0xC0);
    dma.tick_dots(4);
    dma.acknowledge_copy();

    REQUIRE(dma.is_active());
    REQUIRE(dma.source_addr() == 0xC000);
    REQUIRE(dma.oam_offset() == 0);
    REQUIRE(dma.pending_copy_count() == 0);
}

TEST_CASE("OAM DMA releases the bus one M-cycle after the final copy")
{
    OamDma dma;

    dma.start(0xD0);
    dma.tick_dots(644);

    REQUIRE(dma.is_active());
    REQUIRE(dma.pending_copy_count() == 160);

    for (int i = 0; i < 159; ++i) {
        REQUIRE(dma.is_active());
        dma.acknowledge_copy();
    }

    REQUIRE(dma.is_active());
    REQUIRE(dma.oam_offset() == 159);
    REQUIRE(dma.source_addr() == 0xD09F);

    dma.acknowledge_copy();

    REQUIRE(dma.is_active());
    REQUIRE_FALSE(dma.copy_pending());
    REQUIRE(dma.pending_copy_count() == 0);

    dma.tick_dots(3);
    REQUIRE(dma.is_active());

    dma.tick_dots(1);
    REQUIRE_FALSE(dma.is_active());
}

TEST_CASE("OAM DMA does not schedule beyond 160 bytes")
{
    OamDma dma;

    dma.start(0xD0);
    dma.tick_dots(804);

    REQUIRE(dma.pending_copy_count() == 160);
}

TEST_CASE("OAM DMA restart keeps the old transfer alive for the startup M-cycle")
{
    OamDma dma;

    dma.start(0xC0);
    dma.tick_dots(4);
    dma.tick_dots(4);
    dma.acknowledge_copy();
    REQUIRE(dma.source_addr() == 0xC001);

    dma.start(0xD0);
    REQUIRE(dma.read_reg() == 0xD0);
    REQUIRE(dma.source_addr() == 0xC001);

    dma.tick_dots(4);
    REQUIRE(dma.copy_pending());
    REQUIRE(dma.source_addr() == 0xC001);

    dma.acknowledge_copy();
    REQUIRE(dma.is_active());
    REQUIRE(dma.source_addr() == 0xD000);
    REQUIRE(dma.oam_offset() == 0);
    REQUIRE(dma.blocks_cpu_access(0xFE00));
    REQUIRE_FALSE(dma.blocks_cpu_access(0xD000));
}
