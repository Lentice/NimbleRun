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
