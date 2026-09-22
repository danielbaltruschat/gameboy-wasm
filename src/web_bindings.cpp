#include "gameboy.h"
#include "sameboy_dmg_boot_rom.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include <GLES2/gl2.h>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

namespace {

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
    WebGameBoy() { gameboy.load_boot_rom(sameboy::dmg_boot_rom); }
    bool initialize()
    {
        if (initialized) return renderer_ready;
        initialized = true;
        renderer_ready = renderer.initialize();
        if (!renderer_ready) return false;
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, key_callback);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, key_callback);
        emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, blur_callback);
        reset_timing();
        emscripten_set_main_loop_arg(main_loop, this, 0, false);
        return true;
    }
    bool load_boot_rom(uintptr_t address, std::size_t size)
    {
        if (!address || size != BootRom::dmg_size) return false;
        gameboy.load_boot_rom(std::span(reinterpret_cast<const uint8_t*>(address), size));
        reset_if_ready();
        return true;
    }
    bool load_rom(uintptr_t address, std::size_t size)
    {
        if (!address || size < 0x150) return false;
        gameboy.load_rom(std::span(reinterpret_cast<const uint8_t*>(address), size));
        has_rom = true;
        reset_if_ready();
        return true;
    }
    bool ready() const { return has_rom; }
    void reset() { if (ready()) { gameboy.reset(); reset_timing(); present(); } }
    void step_frame() { if (ready()) { gameboy.step_frame(); present(); } }

private:
    static constexpr double frame_duration_ms = 1000.0 / 59.7275;
    GameBoy gameboy;
    WebRenderer renderer;
    std::array<bool, 8> keyboard{};
    bool has_rom = false, initialized = false, renderer_ready = false;
    double previous_tick = 0.0, accumulated_time = 0.0;

    void reset_if_ready() {
        if (ready())
            reset();
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
    static void main_loop(void* data)
    {
        auto* const self = static_cast<WebGameBoy*>(data);
        const double now = emscripten_get_now();
        const double elapsed = std::min(now - self->previous_tick, 250.0);
        self->previous_tick = now;
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
        .function("stepFrame", &WebGameBoy::step_frame);
}
