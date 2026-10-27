# Builds Berkeley SoftFloat 3 as a static library with MSVC, for the EDG front end.
# SoftFloat itself only ships makefiles for GCC and MinGW. The list of source files is taken
# from its Win64-MinGW-w64 makefile, so it stays identical to the upstream build.
#
#   cmake -S <this folder> -B <build folder> -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DSOFTFLOAT_DIR=<SoftFloat checkout>
cmake_minimum_required(VERSION 3.19)
project(softfloat_msvc C)

set(SOFTFLOAT_DIR "" CACHE PATH "SoftFloat source tree (git checkout of berkeley-softfloat-3)")
if(NOT EXISTS "${SOFTFLOAT_DIR}/source/include/softfloat.h")
  message(FATAL_ERROR "SOFTFLOAT_DIR does not point to a SoftFloat checkout: '${SOFTFLOAT_DIR}'")
endif()
set(SPECIALIZE_TYPE 8086-SSE)
set(SRC "${SOFTFLOAT_DIR}/source")

file(READ "${SOFTFLOAT_DIR}/build/Win64-MinGW-w64/Makefile" makefile)
string(REGEX MATCHALL "[A-Za-z0-9_]+\\$\\(OBJ\\)" objects "${makefile}")
list(REMOVE_DUPLICATES objects)

set(sources)
foreach(obj IN LISTS objects)
  string(REPLACE "$(OBJ)" ".c" file "${obj}")
  if(EXISTS "${SRC}/${SPECIALIZE_TYPE}/${file}")
    list(APPEND sources "${SRC}/${SPECIALIZE_TYPE}/${file}")
  elseif(EXISTS "${SRC}/${file}")
    list(APPEND sources "${SRC}/${file}")
  else()
    message(FATAL_ERROR "SoftFloat source not found: ${file}")
  endif()
endforeach()
list(LENGTH sources count)
message(STATUS "SoftFloat: ${count} source files")

add_library(softfloat STATIC ${sources})
# platform.h comes from this folder and replaces the GCC-only one of the MinGW build.
target_include_directories(softfloat PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${SRC}/${SPECIALIZE_TYPE}" "${SRC}/include")
# Same options as the upstream Win64 build, except INLINE_LEVEL: MSVC's C compiler has no
# GCC-style extern inline, so the primitives stay out of line (slower, same results).
target_compile_definitions(softfloat PRIVATE
  SOFTFLOAT_FAST_INT64 SOFTFLOAT_ROUND_ODD INLINE_LEVEL=0
  SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32)
