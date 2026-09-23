#include "gameboy.h"
#include "sameboy_dmg_boot_rom.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <GLES2/gl2.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <picosha2.h>

namespace {

EM_JS(void, set_web_status, (const char* message), {
    if (globalThis.gameboySetStatus) globalThis.gameboySetStatus(UTF8ToString(message));
});

EM_JS(void, set_web_ready, (int ready), {
    if (globalThis.gameboySetReady) globalThis.gameboySetReady(Boolean(ready));
});

class WebRenderer {
public:
    bool initialize()
    {
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        attributes.alpha = false;
        attributes.antialias = false;
        attributes.depth = false;
        attributes.stencil = false;
        attributes.majorVersion = 1;
        context = emscripten_webgl_create_context("#screen", &attributes);
        if (!context || emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS) return false;
        constexpr std::string_view vertex = "attribute vec2 p; attribute vec2 t; varying vec2 u; void main(){gl_Position=vec4(p,0,1);u=t;}";
        constexpr std::string_view fragment = "precision mediump float; varying vec2 u; uniform sampler2D f; void main(){gl_FragColor=texture2D(f,u).abgr;}";
        const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
        if (!vs || !fs) return false;
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) return false;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Framebuffer::width, Framebuffer::height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        constexpr std::array<float, 16> vertices = {-1,-1,0,1, 1,-1,1,1, -1,1,0,0, 1,1,1,0};
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        position = glGetAttribLocation(program, "p");
        texcoord = glGetAttribLocation(program, "t");
        frame = glGetUniformLocation(program, "f");
        return position >= 0 && texcoord >= 0 && frame >= 0;
    }

    void present(const Framebuffer& framebuffer) const
    {
        if (!program) return;
        emscripten_webgl_make_context_current(context);
        glViewport(0, 0, Framebuffer::width, Framebuffer::height);
        glUseProgram(program);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Framebuffer::width, Framebuffer::height, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer.pixels());
        glUniform1i(frame, 0);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glEnableVertexAttribArray(position);
        glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(texcoord);
        glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    GLuint program = 0, texture = 0, buffer = 0;
    GLint position = -1, texcoord = -1, frame = -1;
    static GLuint compile(GLenum type, std::string_view source)
    {
        const GLuint shader = glCreateShader(type);
        const char* text = source.data();
        const GLint length = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &text, &length);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) return shader;
        glDeleteShader(shader);
        return 0;
    }
};

class WebGameBoy {
public:
    WebGameBoy()
        : system_clock_origin(epoch_milliseconds()),
          steady_clock_origin(std::chrono::steady_clock::now())
    {
        gameboy.load_boot_rom(sameboy::dmg_boot_rom);
        gameboy.set_rtc_clock(rtc_clock, this);
    }

    bool initialize()
    {
        if (initialized) return renderer_ready;
        initialized = true;
        renderer_ready = renderer.initialize();
        if (!renderer_ready) return false;
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, key_callback);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, key_callback);
        emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, blur_callback);
        emscripten_set_visibilitychange_callback(this, true, visibility_callback);
        emscripten_set_beforeunload_callback(this, before_unload_callback);
        reset_timing();
        emscripten_set_main_loop_arg(main_loop, this, 0, false);
        return true;
    }
    bool load_boot_rom(uintptr_t address, std::size_t size)
    {
        if (!address || size != BootRom::dmg_size) return false;
        gameboy.load_boot_rom(std::span(reinterpret_cast<const uint8_t*>(address), size));
        reset();
        return true;
    }

    bool load_rom(uintptr_t address, std::size_t size)
    {
        if (!address || size < 0x150) return false;
        pending_rom.assign(
            reinterpret_cast<const uint8_t*>(address),
            reinterpret_cast<const uint8_t*>(address) + size
        );

        if (has_rom && (save_in_flight || !save_snapshot.empty() || gameboy.battery_dirty())) {
            set_web_status("Saving current game before loading the cartridge.");
            flush();
        } else {
            install_pending_rom();
        }
        return true;
    }

    bool ready() const { return has_rom && cartridge_ready; }

    void reset()
    {
        if (!ready()) return;
        reset_pending = true;
        flush();
    }

    void step_frame() { if (ready()) { gameboy.step_frame(); present(); } }

    std::size_t export_save_size()
    {
        return ready() && gameboy.has_battery() ? make_portable_save(false).size() : 0;
    }

    bool copy_export_save(uintptr_t address, std::size_t size)
    {
        const std::vector<uint8_t> save = make_portable_save(false);
        if (!address || save.empty() || size != save.size()) return false;
        std::memcpy(reinterpret_cast<void*>(address), save.data(), save.size());
        return true;
    }

    bool import_save(uintptr_t address, std::size_t size)
    {
        if (!ready() || !gameboy.has_battery() || !address || size < timestamp_size) return false;

        const std::span data(reinterpret_cast<const uint8_t*>(address), size);
        const std::size_t ram_size = gameboy.battery_ram().size();
        const std::size_t rtc_size = gameboy.has_rtc() ? rtc_payload_size : 0;
        if (data.size() != timestamp_size + ram_size + rtc_size) return false;

        const uint64_t saved_at = read_u64(data, 0);
        const std::span ram_data = data.subspan(timestamp_size, ram_size);
        if (!gameboy.load_battery_ram(ram_data)) return false;

        if (gameboy.has_rtc()) {
            const Mbc3RtcRegisters registers = read_rtc(data.subspan(timestamp_size + ram_size));
            if (!gameboy.load_rtc_registers(registers)) return false;

            minimum_rtc_utc = std::max(rtc_utc_milliseconds(), saved_at);
            gameboy.advance_rtc_milliseconds(minimum_rtc_utc - saved_at);
            gameboy.rtc_registers(); // Anchor the core clock after catch-up.
        }

        save_snapshot.assign(data.begin(), data.end());
        save_key = rom_key;
        reset_pending = true;
        set_web_status("Imported save. Writing it to browser storage.");
        start_save();
        return true;
    }

private:
    static constexpr double frame_duration_ms = 1000.0 / 59.7275;
    static constexpr char save_database[] = "dmg-console-saves";
    static constexpr std::size_t timestamp_size = 8;
    static constexpr std::size_t rtc_payload_size = 10;
    static constexpr double save_debounce_ms = 1'000.0;
    static constexpr double save_max_dirty_ms = 15'000.0;
    static constexpr double save_retry_ms = 5'000.0;

    struct LoadRequest {
        WebGameBoy* web_gameboy;
        uint64_t generation;
    };

    GameBoy gameboy;
    WebRenderer renderer;
    std::array<bool, 8> keyboard{};
    std::vector<uint8_t> pending_rom;
    std::vector<uint8_t> save_snapshot;
    std::string rom_key;
    std::string save_key;
    uint64_t load_generation = 0;
    uint64_t observed_battery_revision = 0;
    uint64_t system_clock_origin = 0;
    uint64_t minimum_rtc_utc = 0;
    uint64_t snapshot_rtc_utc = 0;
    std::chrono::steady_clock::time_point steady_clock_origin;
    bool has_rom = false, cartridge_ready = false, initialized = false, renderer_ready = false;
    bool save_in_flight = false, reset_pending = false, snapshot_clock_active = false;
    double dirty_started_at = -1.0, last_dirty_change_at = -1.0, retry_at = 0.0;
    double previous_tick = 0.0, accumulated_time = 0.0;

    static uint64_t epoch_milliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    static uint64_t rtc_clock(void* context)
    {
        const auto& self = *static_cast<WebGameBoy*>(context);
        return self.snapshot_clock_active ? self.snapshot_rtc_utc : self.rtc_utc_milliseconds();
    }

    uint64_t rtc_utc_milliseconds() const
    {
        const uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - steady_clock_origin
        ).count();
        return std::max(system_clock_origin + elapsed, minimum_rtc_utc);
    }

    void install_pending_rom()
    {
        if (pending_rom.empty()) return;

        const std::string next_rom_key = picosha2::hash256_hex_string(
            pending_rom.begin(), pending_rom.end()
        );
        gameboy.load_rom(pending_rom);
        gameboy.set_rtc_clock(rtc_clock, this);
        pending_rom.clear();
        has_rom = true;
        cartridge_ready = false;
        observed_battery_revision = 0;
        dirty_started_at = -1.0;
        last_dirty_change_at = -1.0;
        minimum_rtc_utc = 0;
        rom_key = next_rom_key;
        ++load_generation;
        set_web_ready(false);

        if (!gameboy.has_battery()) {
            finish_rom_load();
            return;
        }

        auto* request = new LoadRequest{this, load_generation};
        emscripten_idb_async_load(
            save_database, rom_key.c_str(), request, load_callback, load_error_callback
        );
        set_web_status("Loading browser save.");
    }

    void finish_rom_load()
    {
        cartridge_ready = true;
        gameboy.reset();
        reset_timing();
        present();
        set_web_ready(true);
        set_web_status("Running.");
    }

    static void load_callback(void* argument, void* data, int size)
    {
        std::unique_ptr<LoadRequest> request(static_cast<LoadRequest*>(argument));
        WebGameBoy& self = *request->web_gameboy;
        if (request->generation != self.load_generation) return;

        if (!self.restore_save(std::span(static_cast<const uint8_t*>(data), static_cast<std::size_t>(size)))) {
            set_web_status("Saved data is incompatible. Starting without it.");
        }
        self.finish_rom_load();
    }

    static void load_error_callback(void* argument)
    {
        std::unique_ptr<LoadRequest> request(static_cast<LoadRequest*>(argument));
        WebGameBoy& self = *request->web_gameboy;
        if (request->generation != self.load_generation) return;
        self.finish_rom_load();
    }

    bool restore_save(std::span<const uint8_t> data)
    {
        const std::size_t ram_size = gameboy.battery_ram().size();
        const std::size_t rtc_size = gameboy.has_rtc() ? rtc_payload_size : 0;
        if (data.size() != timestamp_size + ram_size + rtc_size) return false;

        const uint64_t saved_at = read_u64(data, 0);
        if (!gameboy.load_battery_ram(data.subspan(timestamp_size, ram_size))) return false;
        if (!gameboy.has_rtc()) return true;

        if (!gameboy.load_rtc_registers(read_rtc(data.subspan(timestamp_size + ram_size)))) return false;
        minimum_rtc_utc = std::max(rtc_utc_milliseconds(), saved_at);
        gameboy.advance_rtc_milliseconds(minimum_rtc_utc - saved_at);
        gameboy.rtc_registers(); // Anchor the core clock after catch-up.
        return true;
    }

    std::vector<uint8_t> make_portable_save(bool clear_dirty)
    {
        if (!gameboy.has_battery()) return {};

        const uint64_t saved_at = rtc_utc_milliseconds();
        snapshot_rtc_utc = saved_at;
        snapshot_clock_active = true;
        const Mbc3RtcRegisters registers = gameboy.has_rtc()
            ? gameboy.rtc_registers()
            : Mbc3RtcRegisters{};
        const std::vector<uint8_t> ram = clear_dirty
            ? gameboy.take_battery_ram()
            : gameboy.battery_ram();
        snapshot_clock_active = false;
        std::vector<uint8_t> result;
        result.reserve(timestamp_size + ram.size() + (gameboy.has_rtc() ? rtc_payload_size : 0));
        append_u64(result, saved_at);
        result.insert(result.end(), ram.begin(), ram.end());
        if (gameboy.has_rtc()) append_rtc(result, registers);
        return result;
    }

    static void append_u16(std::vector<uint8_t>& output, uint16_t value)
    {
        output.push_back(value & 0xFF);
        output.push_back(value >> 8);
    }

    static void append_u64(std::vector<uint8_t>& output, uint64_t value)
    {
        for (int byte = 0; byte != 8; ++byte) output.push_back(value >> (byte * 8));
    }

    static uint16_t read_u16(std::span<const uint8_t> input, std::size_t offset)
    {
        return static_cast<uint16_t>(input[offset] | (static_cast<uint16_t>(input[offset + 1]) << 8));
    }

    static uint64_t read_u64(std::span<const uint8_t> input, std::size_t offset)
    {
        uint64_t value = 0;
        for (int byte = 0; byte != 8; ++byte) value |= static_cast<uint64_t>(input[offset + byte]) << (byte * 8);
        return value;
    }

    static void append_rtc(std::vector<uint8_t>& output, const Mbc3RtcRegisters& registers)
    {
        append_u16(output, registers.subsecond_ticks);
        append_u16(output, registers.subsecond_remainder);
        output.push_back(registers.seconds);
        output.push_back(registers.minutes);
        output.push_back(registers.hours);
        append_u16(output, registers.days);
        output.push_back((registers.halted ? 0x01 : 0x00) | (registers.day_carry ? 0x02 : 0x00));
    }

    static Mbc3RtcRegisters read_rtc(std::span<const uint8_t> input)
    {
        return {
            read_u16(input, 0), read_u16(input, 2), input[4], input[5], input[6], read_u16(input, 7),
            (input[9] & 0x01) != 0, (input[9] & 0x02) != 0,
        };
    }

    void start_save()
    {
        if (save_in_flight) return;
        if (!save_snapshot.empty() && has_rom && save_key == rom_key && gameboy.battery_dirty()) {
            // A complete newer snapshot supersedes a failed write for this ROM.
            save_snapshot = make_portable_save(true);
        }
        if (save_snapshot.empty()) {
            if (!has_rom || !gameboy.battery_dirty()) {
                complete_pending_actions();
                return;
            }
            save_snapshot = make_portable_save(true);
            save_key = rom_key;
        }
        if (save_key.empty()) return;

        assert(save_snapshot.size() <= static_cast<std::size_t>(INT32_MAX));
        save_in_flight = true;
        emscripten_idb_async_store(
            save_database,
            save_key.c_str(),
            save_snapshot.data(),
            static_cast<int>(save_snapshot.size()),
            this,
            save_success_callback,
            save_error_callback
        );
    }

    static void save_success_callback(void* argument)
    {
        auto& self = *static_cast<WebGameBoy*>(argument);
        self.save_in_flight = false;
        self.save_snapshot.clear();
        self.save_key.clear();
        self.dirty_started_at = -1.0;
        self.last_dirty_change_at = -1.0;
        self.retry_at = 0.0;
        self.complete_pending_actions();
    }

    static void save_error_callback(void* argument)
    {
        auto& self = *static_cast<WebGameBoy*>(argument);
        self.save_in_flight = false;
        self.retry_at = emscripten_get_now() + save_retry_ms;
        set_web_status("Saving failed. The browser will retry.");
        self.complete_pending_actions();
    }

    void flush()
    {
        if (save_in_flight) return;
        if (!save_snapshot.empty() || (has_rom && gameboy.battery_dirty())) {
            start_save();
        } else {
            complete_pending_actions();
        }
    }

    void complete_pending_actions()
    {
        if (!pending_rom.empty()) {
            install_pending_rom();
            return;
        }
        if (reset_pending && ready()) {
            reset_pending = false;
            gameboy.reset();
            reset_timing();
            present();
            set_web_status("Reset.");
        }
    }

    void update_persistence()
    {
        if (!ready() || !gameboy.has_battery()) return;
        const double now = emscripten_get_now();
        const uint64_t revision = gameboy.battery_revision();
        if (gameboy.battery_dirty() && revision != observed_battery_revision) {
            observed_battery_revision = revision;
            if (dirty_started_at < 0) dirty_started_at = now;
            last_dirty_change_at = now;
        }

        if (save_in_flight || now < retry_at) return;
        if (!save_snapshot.empty()) {
            start_save();
            return;
        }
        if (gameboy.battery_dirty() && (
            now - last_dirty_change_at >= save_debounce_ms ||
            now - dirty_started_at >= save_max_dirty_ms
        )) {
            start_save();
        }
    }

    void present() const {
        if (renderer_ready)
            renderer.present(gameboy.framebuffer());
    }
    void reset_timing() {
        previous_tick = emscripten_get_now(); accumulated_time = 0;
    }

    static int button(std::string_view code)
    {
        if (code == "ArrowRight") return 0; if (code == "ArrowLeft") return 1;
        if (code == "ArrowUp") return 2; if (code == "ArrowDown") return 3;
        if (code == "KeyX") return 4; if (code == "KeyZ") return 5;
        if (code == "ShiftLeft" || code == "ShiftRight") return 6;
        return code == "Enter" ? 7 : -1;
    }
    static bool key_callback(int type, const EmscriptenKeyboardEvent* event, void* data)
    {
        const int key = button(event->code);
        if (key < 0) return false;
        auto* const self = static_cast<WebGameBoy*>(data);
        if (type == EMSCRIPTEN_EVENT_KEYDOWN && !event->repeat) self->keyboard[key] = true;
        if (type == EMSCRIPTEN_EVENT_KEYUP) self->keyboard[key] = false;
        self->gameboy.set_button(static_cast<JoypadButton>(key), self->keyboard[key]);
        return true;
    }
    static bool blur_callback(int, const EmscriptenFocusEvent*, void* data)
    {
        auto* const self = static_cast<WebGameBoy*>(data);
        self->keyboard.fill(false);
        for (int key = 0; key != 8; ++key) self->gameboy.set_button(static_cast<JoypadButton>(key), false);
        return false;
    }

    static bool visibility_callback(int, const EmscriptenVisibilityChangeEvent* event, void* data)
    {
        if (event->hidden) static_cast<WebGameBoy*>(data)->flush();
        return false;
    }

    static const char* before_unload_callback(int, const void*, void* data)
    {
        static_cast<WebGameBoy*>(data)->flush();
        return nullptr;
    }

    static void main_loop(void* data)
    {
        auto* const self = static_cast<WebGameBoy*>(data);
        const double now = emscripten_get_now();
        const double elapsed = std::min(now - self->previous_tick, 250.0);
        self->previous_tick = now;
        self->update_persistence();
        if (!self->ready()) return;
        self->accumulated_time += elapsed;
        int frames = 0;
        while (self->accumulated_time >= frame_duration_ms && frames < 3) {
            self->gameboy.step_frame();
            self->accumulated_time -= frame_duration_ms;
            ++frames;
        }
        if (frames) self->present();
    }
};
} // namespace

EMSCRIPTEN_BINDINGS(gameboy_web_bindings)
{
    emscripten::class_<WebGameBoy>("GameBoy")
        .constructor<>()
        .function("initialize", &WebGameBoy::initialize)
        .function("loadBootRom", &WebGameBoy::load_boot_rom)
        .function("loadRom", &WebGameBoy::load_rom)
        .function("ready", &WebGameBoy::ready)
        .function("reset", &WebGameBoy::reset)
        .function("stepFrame", &WebGameBoy::step_frame)
        .function("exportSaveSize", &WebGameBoy::export_save_size)
        .function("copyExportSave", &WebGameBoy::copy_export_save)
        .function("importSave", &WebGameBoy::import_save);
}
