# Runs a program and fails unless it ends with the expected exit code; ctest itself only tells
# zero from non-zero.
#   cmake -DPROGRAM=<file> -DARGS=<arguments separated by ;> -DEXPECTED=<exit code> -P CheckExitCode.cmake
execute_process(
    COMMAND "${PROGRAM}" ${ARGS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
message("${output}${error}")
if (NOT "${result}" STREQUAL "${EXPECTED}")
  message(FATAL_ERROR "Exit code ${result}, expected ${EXPECTED}.")
endif()
