cmake_minimum_required(VERSION 3.16)

project(BroTracker)

set(CMAKE_CXX_STANDARD 17)

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
    src/runtime/storage.cpp
)

target_link_libraries(BroTracker PRIVATE BroTrackerCore)
target_include_directories(BroTracker PRIVATE src)

add_executable(BroTrackerUIPreview
    tools/ui_preview/main.cpp
    src/ui/font.cpp
    src/ui/text_renderer.cpp
    src/ui/pattern_screen.cpp
    src/renderer/framebuffer.cpp
)

target_link_libraries(BroTrackerUIPreview PRIVATE BroTrackerCore)
target_include_directories(BroTrackerUIPreview PRIVATE src)

enable_testing()

add_executable(BroTrackerTests
    tests/test_main.cpp
    tests/test_note_formatter.cpp
    tests/test_tune_loader.cpp
)

target_link_libraries(BroTrackerTests PRIVATE BroTrackerCore)
target_include_directories(BroTrackerTests PRIVATE src tests)

add_test(
    NAME BroTrackerTests
    COMMAND BroTrackerTests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)