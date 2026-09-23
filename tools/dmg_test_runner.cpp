#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "boot_rom.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "dmg_clock.h"
#include "interrupt_controller.h"
#include "keypad.h"
#include "memory.h"
#include "ppu.h"
#include "timer.h"

namespace {

struct Machine {
    InterruptController interrupts;
    BootRom boot_rom;
    Memory memory;
    Cartridge cartridge;
    PPU ppu{interrupts};
    Timer timer{interrupts};
    Joypad joypad{interrupts};
    MemBus bus{boot_rom, cartridge, ppu, timer, joypad, interrupts, memory};
    CPU cpu{&bus, &interrupts, false};
    uint64_t total_dots = 0;
    uint64_t frames = 0;

    void reset(std::span<const uint8_t> rom, std::span<const uint8_t> boot_rom_image,
               bool post_boot)
    {
        cartridge.load_rom(rom);
        if (!boot_rom_image.empty()) {
            boot_rom.load_dmg(boot_rom_image);
        }
        interrupts.reset();
        boot_rom.reset();
        memory.reset();
        cartridge.reset_mapper();
        ppu.reset();
        timer.reset();
        joypad.reset();
        bus.reset();
        cpu.reset();

        if (!post_boot) {
            return;
        }

        cpu.set_af(0x01B0);
        cpu.set_bc(0x0013);
        cpu.set_de(0x00D8);
        cpu.set_hl(0x014D);
        cpu.sp = 0xFFFE;
        cpu.pc = 0x0100;

        // Values normally established by the DMG boot ROM. Boot-ROM-specific
        // tests still require a real boot ROM and are not valid in this mode.
        interrupts.write_if(0xE1);
        bus.write(0xFF40, 0x91);
        bus.write(0xFF41, 0x85);
        bus.write(0xFF47, 0xFC);
        bus.write(0xFF48, 0xFF);
        bus.write(0xFF49, 0xFF);
    }

    static void tick_callback(void* context)
    {
        static_cast<Machine*>(context)->tick_dot();
    }

    void tick_dot()
    {
        timer.tick_dots(1);
        ppu.tick_dots(1);
        bus.tick_dma_dots(1);
        joypad.tick_dots(1);

        ++total_dots;

        if (ppu.is_frame_ready()) {
            ++frames;
            ppu.clear_frame_ready();
        }
    }

    bool step_instruction()
    {
        do {
            if (!cpu.step_m_cycle(tick_callback, this)) {
                return false;
            }
        } while (!cpu.instruction_boundary() && !cpu.is_locked_up());
        return true;
    }

    void set_button(JoypadButton button, bool pressed)
    {
        const uint8_t previous_value = joypad.read();
        joypad.set_button(button, pressed);
        const uint8_t falling_lines = static_cast<uint8_t>(
            previous_value & ~joypad.read() & 0x0F
        );
        if (falling_lines != 0) {
            cpu.wake_from_stop();
        }
    }
};

struct ButtonEvent {
    uint64_t frame;
    JoypadButton button;
    bool pressed;
};

JoypadButton parse_button(const std::string& name)
{
    if (name == "right") return JoypadButton::Right;
    if (name == "left") return JoypadButton::Left;
    if (name == "up") return JoypadButton::Up;
    if (name == "down") return JoypadButton::Down;
    if (name == "a") return JoypadButton::A;
    if (name == "b") return JoypadButton::B;
    if (name == "select") return JoypadButton::Select;
    if (name == "start") return JoypadButton::Start;
    throw std::runtime_error("unknown button: " + name);
}

std::vector<uint8_t> read_rom(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("could not open ROM: " + path.string());
    }

    const auto size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("could not determine ROM size");
    }

    std::vector<uint8_t> rom(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(rom.data()), size);
    if (!input) {
        throw std::runtime_error("could not read ROM");
    }
    return rom;
}

void write_ppm(const std::filesystem::path& path, const Framebuffer& framebuffer)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("could not create screenshot: " + path.string());
    }

    output << "P6\n" << Framebuffer::width << ' ' << Framebuffer::height << "\n255\n";
    for (int index = 0; index < Framebuffer::width * Framebuffer::height; ++index) {
        const uint32_t rgba = framebuffer.pixels()[index];
        const char rgb[] = {
            static_cast<char>(rgba >> 24),
            static_cast<char>(rgba >> 16),
            static_cast<char>(rgba >> 8),
        };
        output.write(rgb, sizeof(rgb));
    }
}

bool has_mooneye_pass_signature(const CPU& cpu)
{
    return cpu.b == 3 && cpu.c == 5 && cpu.d == 8 && cpu.e == 13
        && cpu.h == 21 && cpu.l == 34;
}

bool has_mooneye_fail_signature(const CPU& cpu)
{
    return cpu.b == 0x42 && cpu.c == 0x42 && cpu.d == 0x42
        && cpu.e == 0x42 && cpu.h == 0x42 && cpu.l == 0x42;
}

void print_state(const Machine& machine, const std::string& result)
{
    std::cout << result
              << " dots=" << std::dec << machine.total_dots
              << " frames=" << machine.frames
              << " pc=" << std::hex << std::setfill('0') << std::setw(4) << machine.cpu.pc
              << " af=" << std::setw(4) << machine.cpu.af()
              << " bc=" << std::setw(4) << machine.cpu.bc()
              << " de=" << std::setw(4) << machine.cpu.de()
              << " hl=" << std::setw(4) << machine.cpu.hl()
              << " sp=" << std::setw(4) << machine.cpu.sp
              << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: dmg_test_runner ROM [--frames N] [--max-seconds N]"
                     " [--ppm PATH] [--boot-rom PATH] [--raw-reset] [--trace-pc ADDRESS]"
                     " [--strict-breakpoint]"
                     " [--tap BUTTON:FRAME] [--hold BUTTON:START:END]\n";
        return 2;
    }

    std::filesystem::path rom_path = argv[1];
    std::filesystem::path ppm_path;
    std::filesystem::path boot_rom_path;
    uint64_t target_frames = 0;
    uint64_t max_seconds = 120;
    bool post_boot = true;
    bool strict_breakpoint = false;
    uint32_t trace_pc = 0x10000;
    std::vector<ButtonEvent> button_events;

    for (int arg = 2; arg < argc; ++arg) {
        const std::string option = argv[arg];
        if (option == "--frames" && arg + 1 < argc) {
            target_frames = std::stoull(argv[++arg]);
        } else if (option == "--max-seconds" && arg + 1 < argc) {
            max_seconds = std::stoull(argv[++arg]);
        } else if (option == "--ppm" && arg + 1 < argc) {
            ppm_path = argv[++arg];
        } else if (option == "--boot-rom" && arg + 1 < argc) {
            boot_rom_path = argv[++arg];
            post_boot = false;
        } else if (option == "--raw-reset") {
            post_boot = false;
        } else if (option == "--strict-breakpoint") {
            strict_breakpoint = true;
        } else if (option == "--trace-pc" && arg + 1 < argc) {
            trace_pc = std::stoul(argv[++arg], nullptr, 0);
            if (trace_pc > 0xFFFF) {
                throw std::runtime_error("trace PC is outside the address space");
            }
        } else if (option == "--tap" && arg + 1 < argc) {
            const std::string tap = argv[++arg];
            const std::size_t separator = tap.find(':');
            if (separator == std::string::npos) {
                throw std::runtime_error("tap must use BUTTON:FRAME syntax");
            }
            const JoypadButton button = parse_button(tap.substr(0, separator));
            const uint64_t frame = std::stoull(tap.substr(separator + 1));
            button_events.push_back(ButtonEvent{frame, button, true});
            button_events.push_back(ButtonEvent{frame + 2, button, false});
        } else if (option == "--hold" && arg + 1 < argc) {
            const std::string hold = argv[++arg];
            const std::size_t first_separator = hold.find(':');
            const std::size_t second_separator = hold.find(':', first_separator + 1);
            if (
                first_separator == std::string::npos ||
                second_separator == std::string::npos
            ) {
                throw std::runtime_error("hold must use BUTTON:START:END syntax");
            }
            const JoypadButton button = parse_button(hold.substr(0, first_separator));
            const uint64_t start = std::stoull(hold.substr(
                first_separator + 1,
                second_separator - first_separator - 1
            ));
            const uint64_t end = std::stoull(hold.substr(second_separator + 1));
            if (end <= start) {
                throw std::runtime_error("hold end frame must follow its start frame");
            }
            button_events.push_back(ButtonEvent{start, button, true});
            button_events.push_back(ButtonEvent{end, button, false});
        } else {
            std::cerr << "unknown or incomplete option: " << option << '\n';
            return 2;
        }
    }

    try {
        const std::vector<uint8_t> rom = read_rom(rom_path);
        const std::vector<uint8_t> boot_rom_image = boot_rom_path.empty()
            ? std::vector<uint8_t>{}
            : read_rom(boot_rom_path);
        if (!boot_rom_image.empty() && boot_rom_image.size() != BootRom::dmg_size) {
            throw std::runtime_error("DMG boot ROM must be exactly 256 bytes");
        }
        Machine machine;
        machine.reset(rom, boot_rom_image, post_boot);
        const uint64_t max_dots = max_seconds * dmg::dot_clock_hz;
        std::sort(
            button_events.begin(),
            button_events.end(),
            [](const ButtonEvent& left, const ButtonEvent& right) {
                return left.frame < right.frame;
            }
        );
        std::size_t next_button_event = 0;

        while (machine.total_dots < max_dots) {
            while (
                next_button_event < button_events.size() &&
                button_events[next_button_event].frame <= machine.frames
            ) {
                const ButtonEvent& event = button_events[next_button_event++];
                machine.set_button(event.button, event.pressed);
            }

            const uint16_t instruction_pc = machine.cpu.pc;
            const auto opcode = instruction_pc <= 0x7FFF
                ? machine.cartridge.read_bus(instruction_pc)
                : std::nullopt;

            if (instruction_pc == trace_pc) {
                std::cerr << "before dots=" << machine.total_dots
                          << " pc=" << std::hex << instruction_pc
                          << " dma=" << machine.bus.dma_active()
                          << " a=" << static_cast<int>(machine.cpu.a)
                          << " b=" << static_cast<int>(machine.cpu.b)
                          << " c=" << static_cast<int>(machine.cpu.c)
                          << " d=" << static_cast<int>(machine.cpu.d)
                          << " ly=" << static_cast<int>(machine.ppu.read(0xFF44))
                          << " stat=" << static_cast<int>(machine.ppu.read(0xFF41))
                          << '\n';
            }

            if (!machine.step_instruction()) {
                print_state(machine, machine.cpu.is_stopped() ? "STOPPED" : "NO_CLOCK");
                return 1;
            }
            if (machine.cpu.is_locked_up()) {
                print_state(machine, "LOCKUP");
                return 1;
            }

            const bool breakpoint = opcode && *opcode == 0x40;
            if (breakpoint) {
                if (has_mooneye_pass_signature(machine.cpu)) {
                    if (!ppm_path.empty()) {
                        write_ppm(ppm_path, machine.ppu.get_framebuffer());
                    }
                    print_state(machine, "PASS");
                    return 0;
                }
                if (has_mooneye_fail_signature(machine.cpu)) {
                    if (!ppm_path.empty()) {
                        write_ppm(ppm_path, machine.ppu.get_framebuffer());
                    }
                    print_state(machine, "FAIL");
                    return 1;
                }
                if (strict_breakpoint) {
                    if (!ppm_path.empty()) {
                        write_ppm(ppm_path, machine.ppu.get_framebuffer());
                    }
                    print_state(machine, "FAIL");
                    return 1;
                }
            }

            if (target_frames != 0 && machine.frames >= target_frames) {
                if (!ppm_path.empty()) {
                    write_ppm(ppm_path, machine.ppu.get_framebuffer());
                }
                print_state(machine, "FRAMES");
                return 0;
            }
        }

        if (!ppm_path.empty()) {
            write_ppm(ppm_path, machine.ppu.get_framebuffer());
        }
        print_state(machine, "TIMEOUT");
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
