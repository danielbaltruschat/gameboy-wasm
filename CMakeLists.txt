cmake_minimum_required(VERSION 3.16)

project(gameboy_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(gameboy_core
    "src/apu.cpp"
    "src/bus.cpp"
    "src/cartridge.cpp"
    "src/cpu.cpp"
    "src/cpu_instructions.cpp"
    "src/gameboy.cpp"
    "src/interrupt_controller.cpp"
    "src/keypad.cpp"
    "src/memory.cpp"
    "src/oam_dma.cpp"
    "src/ppu.cpp"
    "src/serial.cpp"
    "src/timer.cpp"
)

target_include_directories(gameboy_core PUBLIC
    "include"
)

target_compile_features(gameboy_core PUBLIC cxx_std_20)

add_executable(gameboy_wasm
    "src/main.cpp"
)

target_link_libraries(gameboy_wasm PRIVATE
    gameboy_core
)

include(CTest)

if(BUILD_TESTING)
  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
  endif()

  add_executable(gameboy_tests
        "tests/tests.cpp"
        "tests/cputests.cpp"
        "tests/interrupt_controller_tests.cpp"
        "tests/oam_dma_tests.cpp"
        "tests/timer_tests.cpp"
    )

  target_link_libraries(gameboy_tests PRIVATE
      gameboy_core
      Catch2::Catch2WithMain
  )

  include(Catch)
  catch_discover_tests(gameboy_tests)
endif()