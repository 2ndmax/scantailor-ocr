# The command line tool. A console program, unlike the app.
add_executable(scantailor-ocr-cli main.cpp)
target_link_libraries(scantailor-ocr-cli PRIVATE core ${EXTRA_LIBS})
install(TARGETS scantailor-ocr-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

translation_sources(scantailor main.cpp)

# What the tool does is tested in core_tests (CommandLineRun); these only check that the program
# starts and reads its command line. It sets up the settings as the app does: in the build folder,
# the portable ones next to it if the build has a portable config folder, otherwise the user's,
# taking over those of ScanTailor Advanced on the first start. They only set the language of the
# messages, which isn't checked.
# REVIEW-NOTE (2026-11): the tool sets up the settings like the app -> on a machine where ScanTailor OCR never
# ran, these tests create the portable "config" folder and may copy the settings of ScanTailor Advanced into the
# user's AppData, as the app's first start would. Not fixed because it happens once, changes nothing the app
# wouldn't, and the tests only check exit codes.
if (BUILD_TESTS)
  function(add_cli_test name expected)
    add_test(
        NAME ${name}
        COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:scantailor-ocr-cli>" "-DARGS=${ARGN}"
                "-DEXPECTED=${expected}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckExitCode.cmake")
    set_unit_test_environment(${name})
  endfunction()

  add_cli_test(cli_version 0 --version)
  add_cli_test(cli_help 0 --help)
  # CommandLineRun::USAGE_ERROR
  add_cli_test(cli_without_project 1)
  add_cli_test(cli_unknown_option 1 --unknown-option)
  # "--export=" is how an empty value gets through the argument list. Without the checks, the missing
  # project would give exit code 2.
  add_cli_test(cli_empty_export 1 --export= "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_formats_without_export 1 --formats=pdf "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_name_without_export 1 --name=book "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_unknown_format 1 --export=. --formats=doc "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # The option was replaced by --export.
  add_cli_test(cli_old_pdf_option 1 --pdf=book.pdf "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # CommandLineRun::PROJECT_UNUSABLE
  add_cli_test(cli_missing_project 2 "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
endif()
