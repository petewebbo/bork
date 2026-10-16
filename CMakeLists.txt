cmake_minimum_required(VERSION 3.16)
project(bork VERSION 0.1.0 LANGUAGES C CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


include(FetchContent)
# PortAudio 19.7 predates CMake 4's minimum-policy requirement.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG v19.7.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.1.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(portaudio fmt)

include(cmake/CPM.cmake)
CPMAddPackage(
    NAME AudioFile
    GITHUB_REPOSITORY adamstark/AudioFile
    VERSION 1.1.3
    GIT_TAG 1.1.3
    OPTIONS "BUILD_TESTS OFF" "BUILD_EXAMPLES OFF"
)

add_executable(bork src/main.cpp)
target_include_directories(bork PUBLIC
    inc
)
target_link_libraries(bork PRIVATE
    fmt::fmt
    portaudio_static
    AudioFile
)

include(CTest)
if(BUILD_TESTING)
    add_executable(portaudio_test tests/portaudio_test.cpp)
    target_include_directories(portaudio_test PRIVATE inc)
    target_link_libraries(portaudio_test PRIVATE portaudio_static)
    add_test(NAME portaudio_wrapper COMMAND portaudio_test)
endif()

