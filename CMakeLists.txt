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

add_library(nimblerun_hotkey STATIC
    src/app_host/hotkey.cpp
)

target_include_directories(nimblerun_hotkey
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_hotkey
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_hotkey
    PUBLIC
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

target_include_directories(nimblerun_catalog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_catalog
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_catalog
    PUBLIC
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

target_include_directories(nimblerun_settings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_settings
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_settings
    PUBLIC
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

target_include_directories(nimblerun_launch
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_launch
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_launch
    PUBLIC
        shell32
)

add_library(nimblerun_usage STATIC
    src/usage/usage_store.cpp
)

target_include_directories(nimblerun_usage
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_usage
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_usage
    PUBLIC
        kernel32
)

add_library(nimblerun_pins STATIC
    src/pins/pin_store.cpp
)
target_include_directories(nimblerun_pins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_pins
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_pins
    PUBLIC
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

target_include_directories(nimblerun_icons
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_icons
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_icons
    PUBLIC
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
)

target_include_directories(nimblerun_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_ui
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_ui
    PUBLIC
        nimblerun_settings
        oleacc
)

add_library(nimblerun_panel_model STATIC
    src/app_host/panel_model.cpp
)

target_include_directories(nimblerun_panel_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_panel_model
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_panel_model
    PUBLIC
        nimblerun_search
)

add_library(nimblerun_icon_request_session STATIC
    src/app_host/icon_request_session.cpp
)

target_include_directories(nimblerun_icon_request_session
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_icon_request_session
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

add_library(nimblerun_snapshot_assembler STATIC
    src/app_host/snapshot_assembler.cpp
)

target_include_directories(nimblerun_snapshot_assembler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_snapshot_assembler
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_snapshot_assembler
    PUBLIC
        nimblerun_catalog
        nimblerun_usage
        nimblerun_pins
        nimblerun_panel_model
)

add_library(nimblerun_diagnostics STATIC
    src/diagnostics/diagnostic_log.cpp
)

target_include_directories(nimblerun_diagnostics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(nimblerun_diagnostics
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

target_link_libraries(nimblerun_diagnostics
    PUBLIC
        kernel32
)

add_library(nimblerun_rebuild_pipeline STATIC
    src/app_host/rebuild_pipeline.cpp
)

target_include_directories(nimblerun_rebuild_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(nimblerun_rebuild_pipeline PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(nimblerun_rebuild_pipeline PUBLIC nimblerun_catalog kernel32 user32)

add_executable(NimbleRun WIN32
    src/app_host/main.cpp
    src/app_host/settings_dialog.cpp
    src/app_host/catalog_watcher.cpp
    src/resources/NimbleRun.rc
)

target_link_libraries(NimbleRun
    PRIVATE
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
        nimblerun_icon_request_session
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
    target_compile_options(nimblerun_hotkey PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_catalog PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_settings PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_launch PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_usage PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_pins PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_panel_model PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_icon_request_session PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_snapshot_assembler PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_diagnostics PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_icons PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(nimblerun_ui PRIVATE /W4 /permissive- /EHsc)
    target_link_options(NimbleRun PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    set_property(TARGET NimbleRun PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(NimbleRun PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_search PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_hotkey PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_catalog PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_settings PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_launch PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_usage PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_pins PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_panel_model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_icon_request_session PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_snapshot_assembler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_diagnostics PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_icons PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nimblerun_ui PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(NimbleRun PRIVATE -municode -static -Wl,--dynamicbase -Wl,--nxcompat)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
