# NR-055: 22 test targets used to carry a verbatim copy of the same 25 lines of
# definitions, warning flags and link options. The only thing that actually
# varied was the source file and the libraries. A list plus one loop keeps the
# per-target facts visible in one place and makes a toolchain change a one-line
# edit instead of 22 identical ones -- the drift this prevents is silent: a
# missed target simply compiles under different flags with no error.
#
# Each entry: <test name>|<source file>|<libraries, space separated>
# nimblerun_panel_model_test is NOT in this list; see the exception below.
set(NIMBLERUN_TESTS
    "nimblerun_search_test|unit/search_engine_test.cpp|nimblerun_search"
    "nimblerun_hotkey_test|unit/hotkey_registration_test.cpp|nimblerun_hotkey"
    "nimblerun_start_menu_catalog_test|unit/start_menu_catalog_test.cpp|nimblerun_catalog"
    "nimblerun_settings_test|unit/settings_store_test.cpp|nimblerun_settings nimblerun_catalog nimblerun_usage nimblerun_pins nimblerun_icons nimblerun_diagnostics"
    "nimblerun_appsfolder_catalog_test|unit/appsfolder_catalog_test.cpp|nimblerun_catalog"
    "nimblerun_app_filter_test|unit/app_filter_test.cpp|nimblerun_catalog"
    "nimblerun_user_folder_catalog_test|unit/user_folder_catalog_test.cpp|nimblerun_catalog"
    "nimblerun_identity_dedup_test|unit/identity_dedup_test.cpp|nimblerun_catalog"
    "nimblerun_shell_launch_test|unit/shell_launch_test.cpp|nimblerun_launch"
    "nimblerun_recent_usage_test|unit/recent_usage_test.cpp|nimblerun_usage"
    "nimblerun_icons_cache_test|unit/icon_cache_test.cpp|nimblerun_icons"
    "nimblerun_icon_pack_format_test|unit/icon_pack_format_test.cpp|nimblerun_icons"
    # NR-035. Named nimblerun_icon_store_test so `ctest -R icon_store` matches.
    "nimblerun_icon_store_test|unit/icon_store_test.cpp|nimblerun_icons"
    # NR-034. Named nimblerun_png_codec_test so `ctest -R png_codec` matches.
    "nimblerun_png_codec_test|unit/png_codec_test.cpp|nimblerun_icons"
    # NR-032. Named nimblerun_icon_worker_test so `ctest -R icons` matches.
    "nimblerun_icon_worker_test|unit/icon_worker_test.cpp|nimblerun_icons"
    "nimblerun_handoff_registry_test|unit/handoff_registry_test.cpp|nimblerun_icons"
    "nimblerun_dpi_theme_accessibility_test|unit/ui_palette_layout_test.cpp|nimblerun_ui nimblerun_panel_model"
    "nimblerun_settings_ui_test|unit/settings_editor_test.cpp|nimblerun_settings nimblerun_usage"
    "nimblerun_startup_option_test|unit/startup_option_test.cpp|nimblerun_settings"
    "nimblerun_catalog_refresh_test|unit/catalog_refresh_test.cpp|nimblerun_catalog"
    "nimblerun_directory_walker_test|unit/directory_walker_test.cpp|nimblerun_catalog"
    # NR-018. Named nimblerun_pinning_test (not *_pin_store_*) so `ctest -R pinning`
    # matches the CTest name regex.
    "nimblerun_pinning_test|unit/pin_store_test.cpp|nimblerun_pins nimblerun_panel_model"
    "nimblerun_diagnostic_log_test|unit/diagnostic_log_test.cpp|nimblerun_diagnostics"
    # NR-089. Named nimblerun_hotkey_capture_test so `ctest -R capture` matches.
    "nimblerun_hotkey_capture_test|unit/hotkey_capture_test.cpp|nimblerun_settings"
)

# The exception block below must stay in its original registration position so
# ctest test numbers do not shift; the loop is split around it.
list(SUBLIST NIMBLERUN_TESTS 0 10 NIMBLERUN_TESTS_HEAD)
list(SUBLIST NIMBLERUN_TESTS 10 13 NIMBLERUN_TESTS_TAIL)

foreach(entry IN LISTS NIMBLERUN_TESTS_HEAD)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 test_name)
    list(GET parts 1 test_source)
    list(GET parts 2 test_libs)
    string(REPLACE " " ";" test_libs "${test_libs}")

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ${test_libs})
    target_compile_definitions(${test_name}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
    )
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_compile_options(${test_name} PRIVATE /W4 /permissive- /EHsc)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(${test_name} PRIVATE -municode -static)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# NR-055 exception: this target registers its CTest test as
# nimblerun_list_vertical_slice_test (NAME differs from the executable name), so
# the loop's single add_test(NAME ${test_name} COMMAND ${test_name}) line cannot
# produce it. Kept verbatim, in its original registration position.
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

foreach(entry IN LISTS NIMBLERUN_TESTS_TAIL)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 test_name)
    list(GET parts 1 test_source)
    list(GET parts 2 test_libs)
    string(REPLACE " " ";" test_libs "${test_libs}")

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE ${test_libs})
    target_compile_definitions(${test_name}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
    )
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        target_compile_options(${test_name} PRIVATE /W4 /permissive- /EHsc)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(${test_name} PRIVATE -municode -static)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

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

# NR-101 exception: catalog_watcher.cpp is compiled into the NimbleRun
# executable, not a library, so this test compiles the watcher source directly
# (the app_host module it belongs to). Registered after the loop so existing
# CTest test numbers do not shift.
add_executable(nimblerun_catalog_watcher_test
    unit/catalog_watcher_test.cpp
    ../src/app_host/catalog_watcher.cpp
)

target_include_directories(nimblerun_catalog_watcher_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(nimblerun_catalog_watcher_test PRIVATE user32 kernel32)

target_compile_definitions(nimblerun_catalog_watcher_test
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
)

if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_catalog_watcher_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_catalog_watcher_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_catalog_watcher_test PRIVATE -municode -static)
endif()

add_test(NAME nimblerun_catalog_watcher_test COMMAND nimblerun_catalog_watcher_test)

# NR-117: header-only seam for the GetMessageW dispatch decision; compiled
# into the test directly (it lives in the app_host module, not a library).
add_executable(nimblerun_message_loop_test
    unit/message_loop_test.cpp
)
target_include_directories(nimblerun_message_loop_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(nimblerun_message_loop_test PRIVATE user32 kernel32)
target_compile_definitions(nimblerun_message_loop_test
    PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_message_loop_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_message_loop_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_message_loop_test PRIVATE -municode -static)
endif()
add_test(NAME nimblerun_message_loop_test COMMAND nimblerun_message_loop_test)

add_executable(nimblerun_rebuild_pipeline_test unit/rebuild_pipeline_test.cpp)
target_include_directories(nimblerun_rebuild_pipeline_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(nimblerun_rebuild_pipeline_test PRIVATE nimblerun_rebuild_pipeline)
target_compile_definitions(nimblerun_rebuild_pipeline_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(nimblerun_rebuild_pipeline_test PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nimblerun_rebuild_pipeline_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(nimblerun_rebuild_pipeline_test PRIVATE -municode -static)
endif()
add_test(NAME nimblerun_rebuild_pipeline_test COMMAND nimblerun_rebuild_pipeline_test)
