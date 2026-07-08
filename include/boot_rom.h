#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

class BootRom {
public:
    static constexpr std::size_t dmg_size = 0x100; //256 bytes long

    void load_dmg(std::span<const uint8_t> data);
    void reset();

    bool present() const;
    bool mapped() const;

    uint8_t read(uint16_t addr) const;

    uint8_t read_disable_register() const;
    void write_disable_register(uint8_t value);

private:
    std::array<uint8_t, dmg_size> data{};
    bool has_data = false;
    bool is_mapped = true;
    uint8_t disable_register = 0;
};
