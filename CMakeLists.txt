cmake_minimum_required(VERSION 3.25)

project(NimbleRun VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(nimblerun_search STATIC
    src/search/search_engine.cpp
)

target_include_directories(nimblerun_search
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_search
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

add_executable(NimbleRun WIN32
    src/app_host/main.cpp
    src/resources/NimbleRun.rc
)

target_link_libraries(NimbleRun
    PRIVATE
        nimblerun_search
        d2d1
        dwrite
        ole32
        shell32
        user32
)

target_compile_definitions(NimbleRun
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(NimbleRun PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_search PRIVATE /W4 /permissive- /EHsc)
    target_link_options(NimbleRun PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    set_property(TARGET NimbleRun PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(NimbleRun PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_search PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(NimbleRun PRIVATE -municode -static -Wl,--dynamicbase -Wl,--nxcompat)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
