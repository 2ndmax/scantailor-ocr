# The command line tool. A console program, unlike the app.
add_executable(scantailor-ocr-cli main.cpp)
target_link_libraries(scantailor-ocr-cli PRIVATE core ${EXTRA_LIBS})
install(TARGETS scantailor-ocr-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

translation_sources(scantailor main.cpp)

# What the tool does is tested in core_tests (CommandLineRun); these only check that the program
# starts and reads its command line. It reads the settings as the app does: in the build folder,
# the portable ones next to it if the build has a portable config folder, otherwise the user's.
# They only set the language of the messages, which isn't checked.
if (BUILD_TESTS)
  function(add_cli_test name expected)
    add_test(
        NAME ${name}
        COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:scantailor-ocr-cli>" "-DARGS=${ARGN}"
                "-DEXPECTED=${expected}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckExitCode.cmake")
    set_unit_test_environment(${name})
  endfunction()

  add_cli_test(cli_version 0 --version)
  add_cli_test(cli_help 0 --help)
  # CommandLineRun::USAGE_ERROR
  add_cli_test(cli_without_project 1)
  add_cli_test(cli_unknown_option 1 --unknown-option)
  # CommandLineRun::PROJECT_UNUSABLE
  add_cli_test(cli_missing_project 2 "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
endif()
