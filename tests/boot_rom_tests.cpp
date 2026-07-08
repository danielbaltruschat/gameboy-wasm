#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>

#include "boot_rom.h"

namespace {

std::array<uint8_t, BootRom::dmg_size> make_boot_rom()
{
    std::array<uint8_t, BootRom::dmg_size> data{};

    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i ^ 0x5A);
    }

    return data;
}

} // namespace

TEST_CASE("BootRom reports no ROM present before loading")
{
    BootRom boot_rom;

    REQUIRE_FALSE(boot_rom.present());
    REQUIRE_FALSE(boot_rom.mapped());
    REQUIRE(boot_rom.read_disable_register() == 0x00);
}

TEST_CASE("BootRom load_dmg marks ROM present and mapped")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);

    REQUIRE(boot_rom.present());
    REQUIRE(boot_rom.mapped());
    REQUIRE(boot_rom.read_disable_register() == 0x00);
}

TEST_CASE("BootRom read returns loaded DMG boot ROM bytes")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);

    REQUIRE(boot_rom.read(0x0000) == data[0x0000]);
    REQUIRE(boot_rom.read(0x0040) == data[0x0040]);
    REQUIRE(boot_rom.read(0x00FE) == data[0x00FE]);
    REQUIRE(boot_rom.read(0x00FF) == data[0x00FF]);
}

TEST_CASE("BootRom load_dmg copies source data")
{
    BootRom boot_rom;
    auto data = make_boot_rom();
    const uint8_t original = data[0x42];

    boot_rom.load_dmg(data);
    data[0x42] = static_cast<uint8_t>(original + 1);

    REQUIRE(boot_rom.read(0x0042) == original);
}

TEST_CASE("BootRom writing zero to disable register leaves boot ROM mapped")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);
    boot_rom.write_disable_register(0x00);

    REQUIRE(boot_rom.read_disable_register() == 0x00);
    REQUIRE(boot_rom.mapped());
}

TEST_CASE("BootRom writing non-zero to disable register unmaps boot ROM")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);
    boot_rom.write_disable_register(0x01);

    REQUIRE(boot_rom.read_disable_register() == 0x01);
    REQUIRE(boot_rom.present());
    REQUIRE_FALSE(boot_rom.mapped());
}

TEST_CASE("BootRom disable register stores full written value")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);
    boot_rom.write_disable_register(0x80);

    REQUIRE(boot_rom.read_disable_register() == 0x80);
    REQUIRE_FALSE(boot_rom.mapped());
}

TEST_CASE("BootRom cannot be remapped by writing zero after disable")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);
    boot_rom.write_disable_register(0x01);
    boot_rom.write_disable_register(0x00);

    REQUIRE(boot_rom.read_disable_register() == 0x00);
    REQUIRE_FALSE(boot_rom.mapped());
}

TEST_CASE("BootRom reset remaps loaded ROM and clears disable register")
{
    BootRom boot_rom;
    const auto data = make_boot_rom();

    boot_rom.load_dmg(data);
    boot_rom.write_disable_register(0x01);

    boot_rom.reset();

    REQUIRE(boot_rom.present());
    REQUIRE(boot_rom.mapped());
    REQUIRE(boot_rom.read_disable_register() == 0x00);
    REQUIRE(boot_rom.read(0x00A5) == data[0x00A5]);
}

TEST_CASE("BootRom load_dmg replaces existing ROM contents and remaps")
{
    BootRom boot_rom;
    auto first = make_boot_rom();
    auto second = make_boot_rom();
    second[0x00] = 0xC3;
    second[0x80] = 0x7E;
    second[0xFF] = 0x42;

    boot_rom.load_dmg(first);
    boot_rom.write_disable_register(0x01);
    boot_rom.load_dmg(second);

    REQUIRE(boot_rom.present());
    REQUIRE(boot_rom.mapped());
    REQUIRE(boot_rom.read_disable_register() == 0x00);
    REQUIRE(boot_rom.read(0x0000) == 0xC3);
    REQUIRE(boot_rom.read(0x0080) == 0x7E);
    REQUIRE(boot_rom.read(0x00FF) == 0x42);
}
