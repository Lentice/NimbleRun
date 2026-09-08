cmake_minimum_required(VERSION 3.25)

project(NimbleRun VERSION 0.1.2 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Every target in this project compiled with the same include path, the same
# Windows macros and the same warning flags; that block used to be copied
# verbatim per target (~14 libraries plus the tests), so a toolchain change was
# a 14-line edit and a missed target silently compiled under different flags.
# An INTERFACE target is the CMake-native way to say it once.
add_library(nimblerun_common INTERFACE)

target_include_directories(nimblerun_common
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_common
    INTERFACE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_common INTERFACE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_common INTERFACE -Wall -Wextra -Wpedantic)
    # Static libraries ignore link options; these reach the executables that
    # consume them (NimbleRun and every test target).
    target_link_options(nimblerun_common INTERFACE -municode -static)
endif()

add_library(nimblerun_search STATIC
    src/search/search_engine.cpp
)

target_link_libraries(nimblerun_search PUBLIC nimblerun_common)

add_library(nimblerun_hotkey STATIC
    src/app_host/hotkey.cpp
)

target_link_libraries(nimblerun_hotkey
    PUBLIC
        nimblerun_common
        user32
)

add_library(nimblerun_catalog STATIC
    src/catalog/app_filter.cpp
    src/catalog/directory_walker.cpp
    src/catalog/start_menu_catalog.cpp
    src/catalog/appsfolder_catalog.cpp
    src/catalog/user_folder_catalog.cpp
    src/catalog/dedup.cpp
    src/catalog/catalog_refresh.cpp
    src/catalog/catalog_cache.cpp
)

target_link_libraries(nimblerun_catalog
    PUBLIC
        nimblerun_common
        ole32
        shell32
        uuid
)

add_library(nimblerun_settings STATIC
    src/settings/settings_store.cpp
    src/settings/settings_editor.cpp
    src/settings/hotkey_capture.cpp
    src/settings/startup_option.cpp
)

target_link_libraries(nimblerun_settings
    PUBLIC
        nimblerun_common
        kernel32
        advapi32
        shell32
    PRIVATE
        # NR-144: settings_store.cpp's IsLocalAbsolutePath delegates to
        # IsDisplayablePath (app_filter.cpp).
        nimblerun_catalog
)

# user_folder_catalog consumes the settings types and DefaultExtensions().
target_link_libraries(nimblerun_catalog PUBLIC nimblerun_settings)

# catalog_refresh fills AppEntry::normalized_name via search_engine (NR-038).
target_link_libraries(nimblerun_catalog PUBLIC nimblerun_search)

add_library(nimblerun_launch STATIC
    src/launch/shell_launch.cpp
)

target_link_libraries(nimblerun_launch
    PUBLIC
        nimblerun_common
        shell32
)

add_library(nimblerun_usage STATIC
    src/usage/usage_store.cpp
)

target_link_libraries(nimblerun_usage
    PUBLIC
        nimblerun_common
        kernel32
)

add_library(nimblerun_pins STATIC
    src/pins/pin_store.cpp
)

target_link_libraries(nimblerun_pins
    PUBLIC
        nimblerun_common
        kernel32
)

add_library(nimblerun_icons STATIC
    src/icons/icon_cache.cpp
    src/icons/icon_pack_format.cpp
    src/icons/icon_store.cpp
    src/icons/icon_worker.cpp
    src/icons/png_codec.cpp
    src/icons/shell_icon_provider.cpp
)

target_link_libraries(nimblerun_icons
    PUBLIC
        nimblerun_common
        shell32
        ole32
        uuid
        gdi32
        windowscodecs
        user32
        nimblerun_diagnostics
)

add_library(nimblerun_ui STATIC
    src/ui/panel_layout.cpp
    src/ui/panel_accessibility.cpp
    src/ui/panel_palette.cpp
    src/ui/pin_drag_state.cpp
    src/ui/cell_tooltip.cpp
    src/ui/input_mode.cpp
)

target_link_libraries(nimblerun_ui
    PUBLIC
        nimblerun_common
        nimblerun_settings
        oleacc
        # NR-190: input_mode.cpp -- TSF (ole32/uuid, CoCreateInstance +
        # IID/CLSID definitions) with the IMM32 fallback (imm32).
        imm32
        ole32
        uuid
)

# icon_request_session.cpp is a 33-line UI-thread-owned model with no
# dependencies; it lived in a static library of its own until it was folded in
# here, next to the other app_host panel-side model code.
add_library(nimblerun_panel_model STATIC
    src/app_host/panel_model.cpp
    src/app_host/icon_request_session.cpp
)

target_link_libraries(nimblerun_panel_model
    PUBLIC
        nimblerun_common
        nimblerun_search
)

add_library(nimblerun_snapshot_assembler STATIC
    src/app_host/snapshot_assembler.cpp
)

target_link_libraries(nimblerun_snapshot_assembler
    PUBLIC
        nimblerun_common
        nimblerun_catalog
        nimblerun_usage
        nimblerun_pins
        nimblerun_panel_model
)

add_library(nimblerun_diagnostics STATIC
    src/diagnostics/diagnostic_log.cpp
)

target_link_libraries(nimblerun_diagnostics
    PUBLIC
        nimblerun_common
        kernel32
)

add_library(nimblerun_rebuild_pipeline STATIC
    src/app_host/rebuild_pipeline.cpp
)

target_link_libraries(nimblerun_rebuild_pipeline PUBLIC nimblerun_common nimblerun_catalog kernel32 user32)

add_executable(NimbleRun WIN32
    src/app_host/main.cpp
    src/app_host/settings_dialog.cpp
    src/app_host/catalog_watcher.cpp
    src/resources/NimbleRun.rc
)

target_link_libraries(NimbleRun
    PRIVATE
        nimblerun_common
        nimblerun_panel_model
        nimblerun_catalog
        nimblerun_settings
        nimblerun_launch
        nimblerun_usage
        nimblerun_pins
        nimblerun_icons
        nimblerun_ui
        nimblerun_diagnostics
        nimblerun_snapshot_assembler
        nimblerun_rebuild_pipeline
        nimblerun_hotkey
        comctl32
        d2d1
        dwrite
        dwmapi
        ole32
        shell32
        shcore
        user32
        version
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_link_options(NimbleRun PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    set_property(TARGET NimbleRun PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(NimbleRun PRIVATE -Wl,--dynamicbase -Wl,--nxcompat)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
