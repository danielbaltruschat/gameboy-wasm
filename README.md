# gameboy-wasm

Try it [here](https://danielbaltruschat.github.io/gameboy-wasm/).

## What's implemented

- **DMG hardware:** CPU, memory bus, timer, joypad, DMA, PPU, and boot-ROM
  execution emulated per dot-clock cycle.
- **Cartridges:** ROM-only, MBC1/MBC1M, MBC2, MBC3/MBC30 with RTC, and MBC5 modes are supported. Battery-backed RAM and RTC state persist in the browser.

## TODO

- **APU:** audio processing unit to produce browser audio output.
- **Serial:** emulating connection and communication with other GameBoy consoles.
- **Additional cartridge hardware:** MMM01, MBC6, MBC7, Camera, Tama5, HuC1,
  HuC3, and their controller-specific peripherals.

## External tests

The following external DMG test ROMs have passed using the native
`dmg_test_runner` with a DMG boot ROM:

- DMG Mooneye suite: 62/62 ROMs. This includes CPU instruction and
  interrupt timing, timer, OAM DMA, MBC, and PPU timing cases supported by the
  DMG target.
- Blargg: `cpu_instrs`, `instr_timing`, `mem_timing`, `mem_timing-2`,
  `halt_bug`, and `oam_bug`.
- `dmg-acid2` all tests.

The following do not pass:

- Blargg `dmg_sound` (APU not implemented).
- Serial-dependent Mooneye ROMs (Serial not implemented).

Third-party test ROMs are not included in this repository. Obtain them from
their respective upstream projects before running them locally.

## Browser features

The browser fully runs all the emulation logic and WebGL presentation. SameBoy's open compatibility boot ROM
is bundled; see `THIRD_PARTY_NOTICES.md`. A game ROM is not provided due to
copyright.

### Build it yourself

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
and [vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started), then set
`VCPKG_ROOT` and `EMSCRIPTEN_ROOT` to their installation roots. Build and serve
the generated static site:

```sh
cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSCRIPTEN_ROOT/cmake/Modules/Platform/Emscripten.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_ROOT/triplets/community"
cmake --build build-web
python3 -m http.server --directory build-web/web 8080
```

Then open <http://localhost:8080>.

Separate native test build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
ctest --test-dir build --output-on-failure
```
