# Included via CMAKE_PROJECT_INCLUDE by msvc-conformance.ps1 (run 04), right after project().
# The project sets CMAKE_CXX_STANDARD 17 as a normal variable, so it can't be overridden from
# the command line. Instead the compiler option CMake uses for C++17 is redirected to C++20.
set(CMAKE_CXX17_STANDARD_COMPILE_OPTION "-std:c++20")
set(CMAKE_CXX17_EXTENSION_COMPILE_OPTION "-std:c++20")
