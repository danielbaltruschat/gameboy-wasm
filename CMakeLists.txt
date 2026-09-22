cmake_minimum_required(VERSION 3.16)

project(gameboy_wasm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(gameboy_core
    "src/apu.cpp"
    "src/boot_rom.cpp"
    "src/bus.cpp"
    "src/cartridge.cpp"
    "src/cpu.cpp"
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

if(EMSCRIPTEN)
  add_executable(gameboy_web
      "src/web_bindings.cpp"
  )

  target_link_libraries(gameboy_web PRIVATE
      gameboy_core
  )

  set_target_properties(gameboy_web PROPERTIES
      OUTPUT_NAME "gameboy"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web"
  )

  target_link_options(gameboy_web PRIVATE
      "--bind"
      "-sALLOW_MEMORY_GROWTH=1"
      "-sENVIRONMENT=web"
      "-sEXPORT_ES6=1"
      "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
      "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
      "-sFILESYSTEM=0"
      "-sMODULARIZE=1"
  )

  set(web_output_directory "${CMAKE_BINARY_DIR}/web")
  set(web_assets)
  foreach(web_asset IN ITEMS index.html app.js style.css)
    add_custom_command(
        OUTPUT "${web_output_directory}/${web_asset}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${web_output_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/${web_asset}"
            "${web_output_directory}/${web_asset}"
        DEPENDS "${CMAKE_SOURCE_DIR}/web/${web_asset}"
        VERBATIM
    )
    list(APPEND web_assets "${web_output_directory}/${web_asset}")
  endforeach()

  add_custom_target(gameboy_web_assets ALL DEPENDS ${web_assets})
  add_dependencies(gameboy_web gameboy_web_assets)
else()
  add_executable(gameboy_wasm
      "src/main.cpp"
  )

  target_link_libraries(gameboy_wasm PRIVATE
      gameboy_core
  )

  add_executable(dmg_test_runner
      "tools/dmg_test_runner.cpp"
  )

  target_link_libraries(dmg_test_runner PRIVATE
      gameboy_core
  )
endif()

include(CTest)

if(BUILD_TESTING)
  find_package(Catch2 3 REQUIRED)

  add_executable(gameboy_tests
        "tests/tests.cpp"
        "tests/boot_rom_tests.cpp"
        "tests/bus_tests.cpp"
        "tests/cartridge_tests.cpp"
        "tests/cputests.cpp"
        "tests/gameboy_tests.cpp"
        "tests/interrupt_controller_tests.cpp"
        "tests/joypad_tests.cpp"
        "tests/memory_tests.cpp"
        "tests/oam_dma_tests.cpp"
        "tests/ppu_tests.cpp"
        "tests/timer_tests.cpp"
    )

  target_link_libraries(gameboy_tests PRIVATE
      gameboy_core
      Catch2::Catch2WithMain
  )

  include(Catch)
  catch_discover_tests(gameboy_tests)
endif()
