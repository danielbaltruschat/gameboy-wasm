`cartridge.h` is already structured well for a staged implementation. I’d implement it in this order.

**1. Load And Header**
Implement first:
```cpp
Cartridge::Cartridge(std::vector<uint8_t> rom)
Cartridge::load_rom(...)
Cartridge::loaded()
Cartridge::get_header()
Cartridge::get_capabilities()
parse_header()
configure_mapper()
reset_mapper()
```

`load_rom()` should copy ROM bytes, parse header fields from `$0100-$014F`, configure mapper/capabilities, allocate external RAM, then reset mapper state.

Use `assert` for programmer preconditions like minimum ROM size.

**2. ROM-Only Path**
Implement `read()`/`write()` for `MapperType::RomOnly` first.

Address contract:
```text
0000-7FFF = ROM
A000-BFFF = external cartridge RAM
```

For ROM-only:
```text
0000-7FFF -> direct ROM byte
A000-BFFF -> RAM if present/enabled, otherwise likely 0xFF
writes to ROM area -> ignored
```

**3. Shared RAM Helpers**
Implement:
```cpp
read_external_ram()
write_external_ram()
effective_ram_offset()
```

These should handle:
```text
RAM disabled
no RAM
RAM bank selection
out-of-range bank wrapping/clamping
```

I’d use modulo for physical ROM/RAM sizes so malformed headers do not crash debug builds unintentionally, but assert valid bus ranges.

**4. MBC1**
Implement MBC1 next because it is common.

Register map:
```text
0000-1FFF: RAM enable
2000-3FFF: lower 5 ROM bank bits
4000-5FFF: upper ROM bank bits or RAM bank
6000-7FFF: banking mode
A000-BFFF: external RAM
```

Important quirks:
```text
ROM bank 0 cannot be selected for switchable bank; remap 0 -> 1
banking_mode 0: larger ROM banking
banking_mode 1: RAM banking / upper bits affect lower ROM area on some carts
```

**5. MBC2**
MBC2 has built-in 512 x 4-bit RAM, not normal external RAM.

Key behavior:
```text
RAM enable writes only when address bit 8 is 0
ROM bank writes only when address bit 8 is 1
RAM stores only lower nibble
RAM reads usually return upper nibble as 0xF
```

Your `Mbc2State::ram` already supports this.

**6. MBC3 + RTC**
Implement normal MBC3 banking first, RTC second.

Register map:
```text
0000-1FFF: RAM/RTC enable
2000-3FFF: ROM bank
4000-5FFF: RAM bank or RTC register select
6000-7FFF: RTC latch
A000-BFFF: RAM or selected RTC register
```

RTC register select:
```text
08 = seconds
09 = minutes
0A = hours
0B = lower day
0C = upper day / halt / carry
```

`tick_rtc_seconds()` can update live RTC state. Latching copies live RTC into latched fields on `0 -> 1` latch transition.

**7. MBC5**
MBC5 is simpler than MBC3.

Register map:
```text
0000-1FFF: RAM enable
2000-2FFF: lower 8 ROM bank bits
3000-3FFF: 9th ROM bank bit
4000-5FFF: RAM bank / rumble bit
A000-BFFF: external RAM
```

No `0 -> 1` bank fixup like MBC1; bank 0 is selectable.

**8. Tests**
Add tests in this order:
```text
header parsing
ROM-only read
RAM enable/read/write
MBC1 ROM banking
MBC1 RAM banking
MBC2 nibble RAM
MBC3 ROM/RAM banking
MBC3 RTC latch/read/write
MBC5 9-bit ROM banking
```

Avoid trying to implement all MBCs at once. Get ROM-only + MBC1 solid first; that unlocks many early games and keeps debugging contained.

## User

What functions do I change to implement MBC

## Assistant

Implement MBC behavior mainly in these `Cartridge` methods from `cartridge.h`:

```cpp
void configure_mapper();
void reset_mapper();

uint8_t read(uint16_t address) const;
void write(uint16_t address, uint8_t value);

std::size_t effective_rom_offset(uint16_t address) const;
std::size_t effective_ram_offset(uint16_t address) const;

uint8_t read_external_ram(uint16_t address) const;
void write_external_ram(uint16_t address, uint8_t value);
```

For specific MBCs:

```cpp
configure_mapper()
```

Map `header.cartridgeType` to:

```cpp
capabilities.mapper
capabilities.hasRam
capabilities.hasBattery
capabilities.hasTimer
capabilities.hasRumble
```

Also allocate `ram` based on `header.ramSizeBytes`, except MBC2 which uses `mbc2.ram`.

```cpp
reset_mapper()
```

Reset runtime mapper state:

```cpp
ram_enabled = false;
rom_bank = 1;
ram_bank = 0;
rtc_register_select = 0;
mbc1 = {};
mbc2 = {};
mbc3_rtc = {};
mbc5 = {};
```

Then restore non-zero defaults where needed:

```cpp
mbc1.lower_rom_bank_bits = 1;
mbc5.rom_bank = 1;
```

```cpp
read(address)
```

Route reads:

```text
0000-7FFF -> ROM through effective_rom_offset()
A000-BFFF -> external RAM or RTC
otherwise -> 0xFF / assert, depending on bus contract
```

```cpp
write(address, value)
```

This is where most MBC register writes happen:

```text
0000-1FFF -> RAM enable
2000-3FFF -> ROM bank select
4000-5FFF -> RAM bank / upper ROM bits / RTC select
6000-7FFF -> banking mode / RTC latch
A000-BFFF -> external RAM / RTC write
```

Switch behavior based on:

```cpp
capabilities.mapper
```

```cpp
effective_rom_offset(address)
```

Calculate the physical ROM vector index after banking.

For example:

```text
0000-3FFF -> fixed bank or MBC1 banked lower area
4000-7FFF -> selected ROM bank
```

```cpp
effective_ram_offset(address)
```

Calculate physical RAM index:

```text
A000-BFFF offset within selected RAM bank
```

```cpp
read_external_ram() / write_external_ram()
```

Handle:

```text
RAM disabled
no RAM
MBC2 internal nibble RAM
MBC3 RTC selected instead of RAM
normal external RAM
```

For MBC3 RTC specifically, also implement:

```cpp
uint8_t read_rtc_register() const;
void write_rtc_register(uint8_t value);
void tick_rtc_seconds(uint32_t seconds);
```

So the implementation order should be:

```text
1. parse_header()
2. configure_mapper()
3. reset_mapper()
4. read()/write() ROM-only
5. effective_rom_offset()
6. external RAM helpers
7. MBC1 write banking
8. MBC2
9. MBC3 + RTC
10. MBC5
```
