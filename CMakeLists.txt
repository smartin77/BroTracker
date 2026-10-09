cmake_minimum_required(VERSION 3.13)

project(BroTracker)

set(CMAKE_CXX_STANDARD 17)

option(
    BROTRACKER_BUILD_ARKOS_UI
    "Build BroTracker Terminal for ArkOS/Linux"
    OFF
)

option(BROTRACKER_BUILD_WINDOWS_TERMINAL "Build BroTracker Terminal for Windows" OFF)
if(BROTRACKER_BUILD_ARKOS_UI AND BROTRACKER_BUILD_WINDOWS_TERMINAL)
    message(FATAL_ERROR "Select one platform build for BroTrackerTerminal")
endif()
if(BROTRACKER_BUILD_WINDOWS_TERMINAL AND NOT WIN32)
    message(FATAL_ERROR "BroTracker Windows Terminal requires Windows")
endif()

# Shared temporary host protocol and controls; platform transport linked separately.
add_library(BroTrackerBringUp STATIC src/ui/bringup_serial.cpp src/ui/bringup_controls.cpp)
target_include_directories(BroTrackerBringUp PUBLIC src)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BROTRACKER_SERIAL_TRANSPORT ui/arkos/serial_transport.cpp)
else()
    set(BROTRACKER_SERIAL_TRANSPORT src/ui/serial_unavailable.cpp)
endif()

# Platform-independent tracker core, shared by the UI executable and tests.
add_library(BroTrackerCore STATIC
    src/core/logger.cpp
    src/core/tune_loader.cpp
    src/core/note_formatter.cpp
    src/core/note_names.cpp
)

target_include_directories(BroTrackerCore PUBLIC src)

add_executable(BroTracker
    src/main.cpp
    src/runtime/runtime.cpp
    libraries/scheduler/src/scheduler.cpp
    libraries/scheduler/src/musical_timing.cpp
    libraries/scheduler/src/musical_tick_cursor.cpp
    libraries/scheduler/src/pattern_position.cpp
    src/runtime/storage.cpp
)

target_link_libraries(BroTracker PRIVATE BroTrackerCore)
target_include_directories(BroTracker PRIVATE src libraries/scheduler/src)

add_executable(BroTrackerUIPreview
    tools/ui_preview/main.cpp
    src/ui/font.cpp
    src/ui/text_renderer.cpp
    src/ui/pattern_screen.cpp
    src/renderer/framebuffer.cpp
)

target_link_libraries(BroTrackerUIPreview PRIVATE BroTrackerCore)
target_include_directories(BroTrackerUIPreview PRIVATE src)

if(BROTRACKER_BUILD_ARKOS_UI OR BROTRACKER_BUILD_WINDOWS_TERMINAL)
    find_package(SDL2 CONFIG QUIET)

    if(TARGET SDL2::SDL2)
        set(BROTRACKER_SDL2_TARGET SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            set(BROTRACKER_SDL2_MAIN_TARGET SDL2::SDL2main)
        endif()
    elseif(TARGET SDL2::SDL2-static)
        set(BROTRACKER_SDL2_TARGET SDL2::SDL2-static)
        if(TARGET SDL2::SDL2main)
            set(BROTRACKER_SDL2_MAIN_TARGET SDL2::SDL2main)
        endif()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(BROTRACKER_SDL2_TARGET PkgConfig::SDL2)
    endif()

endif()

if(BROTRACKER_BUILD_ARKOS_UI)
    add_executable(BroTrackerTerminal
        ui/arkos/main.cpp
        ${BROTRACKER_SERIAL_TRANSPORT}
        ui/sdl/sdl_display_backend.cpp
        src/ui/font.cpp
        src/ui/text_renderer.cpp
        src/ui/pattern_screen.cpp
        src/renderer/framebuffer.cpp
    )

    target_link_libraries(BroTrackerTerminal PRIVATE
        BroTrackerCore BroTrackerBringUp
        ${BROTRACKER_SDL2_MAIN_TARGET}
        ${BROTRACKER_SDL2_TARGET}
    )

    target_include_directories(BroTrackerTerminal PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(BROTRACKER_BUILD_WINDOWS_TERMINAL)
    find_package(Threads REQUIRED)
    add_executable(BroTrackerTerminal WIN32
        ui/windows/main.cpp ui/windows/serial_transport.cpp ui/windows/audio_bridge.cpp
        ui/sdl/sdl_display_backend.cpp
        src/ui/font.cpp src/ui/text_renderer.cpp src/ui/pattern_screen.cpp
        src/renderer/framebuffer.cpp)
    target_include_directories(BroTrackerTerminal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(BroTrackerTerminal PRIVATE BroTrackerCore BroTrackerBringUp
        ${BROTRACKER_SDL2_MAIN_TARGET} ${BROTRACKER_SDL2_TARGET}
        setupapi advapi32 ole32 uuid avrt Threads::Threads)
endif()

enable_testing()
add_executable(BroTrackerHostClockInputTests tests/test_host_clock_input.cpp)
target_include_directories(BroTrackerHostClockInputTests PRIVATE libraries/teensy_diagnostics/src)
add_test(NAME BroTrackerHostClockInputTests COMMAND BroTrackerHostClockInputTests)
add_executable(BroTrackerBringUpControlsTests tests/test_bringup_controls.cpp)
target_link_libraries(BroTrackerBringUpControlsTests PRIVATE BroTrackerBringUp)
add_test(NAME BroTrackerBringUpControlsTests COMMAND BroTrackerBringUpControlsTests)
if(BROTRACKER_BUILD_WINDOWS_TERMINAL)
    add_executable(BroTrackerWindowsAudioTests tests/test_windows_audio_buffer.cpp)
    target_include_directories(BroTrackerWindowsAudioTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME BroTrackerWindowsAudioTests COMMAND BroTrackerWindowsAudioTests)
    add_executable(BroTrackerWindowsInputTests tests/test_windows_input.cpp)
    target_include_directories(BroTrackerWindowsInputTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(BroTrackerWindowsInputTests PRIVATE BroTrackerBringUp ${BROTRACKER_SDL2_TARGET})
    add_test(NAME BroTrackerWindowsInputTests COMMAND BroTrackerWindowsInputTests)
endif()

add_executable(BroTrackerTests
    tests/test_main.cpp
    tests/test_note_formatter.cpp
    tests/test_tune_loader.cpp
    tests/test_scheduler.cpp
    tests/test_row_events.cpp
    tests/test_channel_state.cpp
    tests/test_ram_sample_voice.cpp
    tests/test_ram_voice_block_renderer.cpp
    tests/test_pcm16_mixer.cpp
    tests/test_native_rate_sample_commands.cpp
    tests/test_native_rate_pattern_player.cpp
    tests/test_pattern_bringup_control.cpp
    libraries/scheduler/src/scheduler.cpp
    libraries/scheduler/src/musical_timing.cpp
    libraries/scheduler/src/musical_tick_cursor.cpp
    libraries/scheduler/src/pattern_position.cpp
)

target_link_libraries(BroTrackerTests PRIVATE BroTrackerCore)
target_include_directories(BroTrackerTests PRIVATE src tests libraries/scheduler/src)

add_test(
    NAME BroTrackerTests
    COMMAND BroTrackerTests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Exercise the real nonblocking Linux transport using a pseudo-terminal.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(BroTrackerBringUpSerialTests
        tests/test_bringup_serial.cpp
        ${BROTRACKER_SERIAL_TRANSPORT}
    )
    target_include_directories(BroTrackerBringUpSerialTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(BroTrackerBringUpSerialTests PRIVATE BroTrackerBringUp)
    add_test(NAME BroTrackerBringUpSerialTests COMMAND BroTrackerBringUpSerialTests)
    set_tests_properties(BroTrackerBringUpSerialTests PROPERTIES TIMEOUT 20)
endif()

# Standalone bring-up experiment; no ALSA dependency in the UI/default build.
option(BROTRACKER_BUILD_ALSA_BRIDGE "Build the temporary Linux ALSA live bridge" OFF)
if(BROTRACKER_BUILD_ALSA_BRIDGE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The ALSA bridge requires Linux")
    endif()
    find_package(ALSA REQUIRED)
    find_package(Threads REQUIRED)
    add_executable(BroTrackerAlsaBridge tools/alsa_bridge/main.cpp)
    target_include_directories(BroTrackerAlsaBridge PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(BroTrackerAlsaBridge PRIVATE ${ALSA_LIBRARIES} Threads::Threads)
    target_compile_options(BroTrackerAlsaBridge PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME BroTrackerAlsaBridgeChecks
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/alsa_bridge/check.sh
            $<TARGET_FILE:BroTrackerAlsaBridge>)
    set_tests_properties(BroTrackerAlsaBridgeChecks PROPERTIES TIMEOUT 30)
    add_executable(BroTrackerRecordingTests tools/alsa_bridge/recording_test.cpp)
    target_link_libraries(BroTrackerRecordingTests PRIVATE Threads::Threads)
    add_test(NAME BroTrackerRecordingTests COMMAND BroTrackerRecordingTests)
    add_executable(BroTrackerAlsaDiscoveryTests tools/alsa_bridge/discovery_test.cpp)
    add_test(NAME BroTrackerAlsaDiscoveryTests COMMAND BroTrackerAlsaDiscoveryTests)
endif()
