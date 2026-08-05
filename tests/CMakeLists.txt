add_executable(nimblerun_search_test
    unit/search_engine_test.cpp
)

target_link_libraries(nimblerun_search_test PRIVATE nimblerun_search)

target_compile_definitions(nimblerun_search_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_search_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_search_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_search_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_search_test COMMAND nimblerun_search_test)

add_executable(nimblerun_hotkey_test
    unit/hotkey_registration_test.cpp
)

target_link_libraries(nimblerun_hotkey_test PRIVATE nimblerun_hotkey)

target_compile_definitions(nimblerun_hotkey_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_hotkey_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_hotkey_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_hotkey_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_hotkey_test COMMAND nimblerun_hotkey_test)

add_executable(nimblerun_start_menu_catalog_test
    unit/start_menu_catalog_test.cpp
)

target_link_libraries(nimblerun_start_menu_catalog_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_start_menu_catalog_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_start_menu_catalog_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_start_menu_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_start_menu_catalog_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_start_menu_catalog_test COMMAND nimblerun_start_menu_catalog_test)

add_executable(nimblerun_settings_test
    unit/settings_store_test.cpp
)

target_link_libraries(nimblerun_settings_test PRIVATE nimblerun_settings)

target_compile_definitions(nimblerun_settings_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_settings_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_settings_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_settings_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_settings_test COMMAND nimblerun_settings_test)

add_executable(nimblerun_appsfolder_catalog_test
    unit/appsfolder_catalog_test.cpp
)

target_link_libraries(nimblerun_appsfolder_catalog_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_appsfolder_catalog_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_appsfolder_catalog_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_appsfolder_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_appsfolder_catalog_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_appsfolder_catalog_test COMMAND nimblerun_appsfolder_catalog_test)

add_executable(nimblerun_app_filter_test
    unit/app_filter_test.cpp
)

target_link_libraries(nimblerun_app_filter_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_app_filter_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_app_filter_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_app_filter_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_app_filter_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_app_filter_test COMMAND nimblerun_app_filter_test)

add_executable(nimblerun_user_folder_catalog_test
    unit/user_folder_catalog_test.cpp
)

target_link_libraries(nimblerun_user_folder_catalog_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_user_folder_catalog_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_user_folder_catalog_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_user_folder_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_user_folder_catalog_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_user_folder_catalog_test COMMAND nimblerun_user_folder_catalog_test)

add_executable(nimblerun_identity_dedup_test
    unit/identity_dedup_test.cpp
)

target_link_libraries(nimblerun_identity_dedup_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_identity_dedup_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_identity_dedup_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_identity_dedup_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_identity_dedup_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_identity_dedup_test COMMAND nimblerun_identity_dedup_test)

add_executable(nimblerun_shell_launch_test
    unit/shell_launch_test.cpp
)

target_link_libraries(nimblerun_shell_launch_test PRIVATE nimblerun_launch)

target_compile_definitions(nimblerun_shell_launch_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_shell_launch_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_shell_launch_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_shell_launch_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_shell_launch_test COMMAND nimblerun_shell_launch_test)

add_executable(nimblerun_recent_usage_test
    unit/recent_usage_test.cpp
)

target_link_libraries(nimblerun_recent_usage_test PRIVATE nimblerun_usage)

target_compile_definitions(nimblerun_recent_usage_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_recent_usage_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_recent_usage_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_recent_usage_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_recent_usage_test COMMAND nimblerun_recent_usage_test)

add_executable(nimblerun_panel_model_test
    unit/panel_model_test.cpp
)

target_link_libraries(nimblerun_panel_model_test PRIVATE nimblerun_panel_model)

target_compile_definitions(nimblerun_panel_model_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_panel_model_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_panel_model_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_panel_model_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_list_vertical_slice_test COMMAND nimblerun_panel_model_test)

add_executable(nimblerun_icons_cache_test
    unit/icon_cache_test.cpp
)

target_link_libraries(nimblerun_icons_cache_test PRIVATE nimblerun_icons)

target_compile_definitions(nimblerun_icons_cache_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_icons_cache_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_icons_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_icons_cache_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_icons_cache_test COMMAND nimblerun_icons_cache_test)

add_executable(nimblerun_dpi_theme_accessibility_test
    unit/ui_palette_layout_test.cpp
)

target_link_libraries(nimblerun_dpi_theme_accessibility_test PRIVATE nimblerun_ui nimblerun_panel_model)

target_compile_definitions(nimblerun_dpi_theme_accessibility_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_dpi_theme_accessibility_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_dpi_theme_accessibility_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_dpi_theme_accessibility_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_dpi_theme_accessibility_test COMMAND nimblerun_dpi_theme_accessibility_test)

add_executable(nimblerun_settings_ui_test
    unit/settings_editor_test.cpp
)

target_link_libraries(nimblerun_settings_ui_test PRIVATE nimblerun_settings nimblerun_usage)

target_compile_definitions(nimblerun_settings_ui_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_settings_ui_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_settings_ui_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_settings_ui_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_settings_ui_test COMMAND nimblerun_settings_ui_test)

add_executable(nimblerun_startup_option_test
    unit/startup_option_test.cpp
)

target_link_libraries(nimblerun_startup_option_test PRIVATE nimblerun_settings)

target_compile_definitions(nimblerun_startup_option_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_startup_option_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_startup_option_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_startup_option_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_startup_option_test COMMAND nimblerun_startup_option_test)

add_executable(nimblerun_catalog_refresh_test
    unit/catalog_refresh_test.cpp
)

target_link_libraries(nimblerun_catalog_refresh_test PRIVATE nimblerun_catalog)

target_compile_definitions(nimblerun_catalog_refresh_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_catalog_refresh_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_catalog_refresh_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_catalog_refresh_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_catalog_refresh_test COMMAND nimblerun_catalog_refresh_test)

# NR-018. Named nimblerun_pinning_test (not *_pin_store_*) so `ctest -R pinning`
# matches the CTest name regex.
add_executable(nimblerun_pinning_test
    unit/pin_store_test.cpp
)

target_link_libraries(nimblerun_pinning_test PRIVATE nimblerun_pins nimblerun_panel_model)

target_compile_definitions(nimblerun_pinning_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_pinning_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_pinning_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_pinning_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_pinning_test COMMAND nimblerun_pinning_test)

add_executable(nimblerun_diagnostic_log_test
    unit/diagnostic_log_test.cpp
)

target_link_libraries(nimblerun_diagnostic_log_test PRIVATE nimblerun_diagnostics)

target_compile_definitions(nimblerun_diagnostic_log_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_diagnostic_log_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_diagnostic_log_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_diagnostic_log_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_diagnostic_log_test COMMAND nimblerun_diagnostic_log_test)

if(CMAKE_HOST_WIN32)
    find_program(PWSH_EXECUTABLE pwsh)
    if(PWSH_EXECUTABLE)
        add_test(
            NAME nimblerun_lifecycle_check
            COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File
                ${CMAKE_CURRENT_SOURCE_DIR}/integration/lifecycle_check.ps1
                -ExePath $<TARGET_FILE:NimbleRun>
        )
    endif()
endif()
