cmake_minimum_required(VERSION 3.16)

project(gameboy_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(gameboy_wasm
    "src/apu.cpp"
    "src/bus.cpp"
    "src/cpu.cpp"
    "src/gameboy.cpp"
    "src/interrupt_controller.cpp"
    "src/cartridge.cpp"
    "src/io.cpp"
    "src/keypad.cpp"
    "src/main.cpp"
    "src/memory.cpp"
    "src/ppu.cpp"
    "src/timer.cpp"
)

target_include_directories(gameboy_wasm PRIVATE
    "include"
)
