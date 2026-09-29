cmake_minimum_required(VERSION 3.13)

project(BroTracker)

set(CMAKE_CXX_STANDARD 17)

option(
    BROTRACKER_BUILD_ARKOS_UI
    "Build the SDL2 ArkOS/Linux UI client"
    OFF
)

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

if(BROTRACKER_BUILD_ARKOS_UI)
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

    add_executable(BroTrackerArkOSUI
        ui/arkos/main.cpp
        ui/arkos/bringup_serial.cpp
        ui/sdl/sdl_display_backend.cpp
        src/ui/font.cpp
        src/ui/text_renderer.cpp
        src/ui/pattern_screen.cpp
        src/renderer/framebuffer.cpp
    )

    target_link_libraries(BroTrackerArkOSUI PRIVATE
        BroTrackerCore
        ${BROTRACKER_SDL2_MAIN_TARGET}
        ${BROTRACKER_SDL2_TARGET}
    )

    target_include_directories(BroTrackerArkOSUI PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

enable_testing()

add_executable(BroTrackerTests
    tests/test_main.cpp
    tests/test_note_formatter.cpp
    tests/test_tune_loader.cpp
    tests/test_scheduler.cpp
    libraries/scheduler/src/scheduler.cpp
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
        ui/arkos/bringup_serial.cpp
    )
    target_include_directories(BroTrackerBringUpSerialTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
    add_executable(BroTrackerAlsaBridge tools/alsa_bridge/main.cpp)
    target_include_directories(BroTrackerAlsaBridge PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(BroTrackerAlsaBridge PRIVATE ${ALSA_LIBRARIES})
    target_compile_options(BroTrackerAlsaBridge PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME BroTrackerAlsaBridgeChecks
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/alsa_bridge/check.sh
            $<TARGET_FILE:BroTrackerAlsaBridge>)
    set_tests_properties(BroTrackerAlsaBridgeChecks PROPERTIES TIMEOUT 15)
    add_executable(BroTrackerAlsaDiscoveryTests tools/alsa_bridge/discovery_test.cpp)
    add_test(NAME BroTrackerAlsaDiscoveryTests COMMAND BroTrackerAlsaDiscoveryTests)
endif()
