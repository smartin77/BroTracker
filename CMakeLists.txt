cmake_minimum_required(VERSION 3.16)

project(BroTrackerUI)

set(CMAKE_CXX_STANDARD 17)

add_executable(BroTrackerUI
    src/main.cpp
    src/ui/font.cpp
    src/ui/text_renderer.cpp
    src/ui/pattern_screen.cpp
    src/core/logger.cpp
    src/core/tune_loader.cpp
    src/core/note_formatter.cpp
    src/core/note_names.cpp
    src/renderer/framebuffer.cpp
)

target_include_directories(BroTrackerUI PRIVATE src)