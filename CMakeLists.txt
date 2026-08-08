cmake_minimum_required(VERSION 3.16)

project(BroTrackerUI)

set(CMAKE_CXX_STANDARD 17)

add_executable(BroTrackerUI
    src/main.cpp
    src/ui/font.cpp
    src/core/logger.cpp
    src/core/dummy_data.cpp
)