# Runs a program and fails unless it ends with the expected exit code; ctest itself only tells
# zero from non-zero.
#   cmake -DPROGRAM=<file> -DARGS=<arguments separated by ;> -DEXPECTED=<exit code>
#         -DSCANTAILOR_CMAKE_MINIMUM_VERSION=<the project's minimum version> -P CheckExitCode.cmake

# A script run with -P doesn't inherit the project's policies.
if (NOT DEFINED SCANTAILOR_CMAKE_MINIMUM_VERSION)
  message(FATAL_ERROR "CheckExitCode.cmake needs -DSCANTAILOR_CMAKE_MINIMUM_VERSION=...")
endif()
cmake_minimum_required(VERSION ${SCANTAILOR_CMAKE_MINIMUM_VERSION})

execute_process(
    COMMAND "${PROGRAM}" ${ARGS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
message("${output}${error}")
if (NOT "${result}" STREQUAL "${EXPECTED}")
  message(FATAL_ERROR "Exit code ${result}, expected ${EXPECTED}.")
endif()
