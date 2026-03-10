cmake_minimum_required(VERSION 3.16)
project(tui VERSION 0.1.0 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


include(../cmake/CPM.cmake)
CPMAddPackage("gh:/a-n-t-h-o-n-y/TermOx#v2.0.0")

add_executable(tui grid_tui.cpp)
# target_include_directories(bork PUBLIC
#     inc
# )
target_link_libraries(tui PRIVATE
    TermOx
)

