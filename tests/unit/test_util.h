// NR-129: the repo-standard test assertion helper, shared by all unit tests.
//
// This is not the standard assert() macro: CMAKE_BUILD_TYPE=Release sets
// NDEBUG, which compiles assert() out -- exactly the configuration AGENTS.md
// tells you to validate with. Expect() is a plain function, so it runs in
// every configuration (NR-048 rationale).

#pragma once

#include <cstdio>
#include <cstdlib>

inline void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}
