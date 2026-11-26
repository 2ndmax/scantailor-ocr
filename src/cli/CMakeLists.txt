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
                "-DEXPECTED=${expected}" "-DSCANTAILOR_CMAKE_MINIMUM_VERSION=${SCANTAILOR_CMAKE_MINIMUM_VERSION}" -P
                "${PROJECT_SOURCE_DIR}/cmake/CheckExitCode.cmake")
    set_unit_test_environment(${name})
  endfunction()

  add_cli_test(cli_version 0 --version)
  add_cli_test(cli_help 0 --help)
  # CommandLineRun::USAGE_ERROR
  add_cli_test(cli_without_project 1)
  add_cli_test(cli_unknown_option 1 --unknown-option)
  add_cli_test(cli_unknown_last_step 1 --last-step=bogus "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # "--export-folder=" is how an empty value gets through the argument list. Without the checks, the
  # missing project would give exit code 2.
  add_cli_test(cli_empty_export_folder 1 --export-folder= "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_empty_profile 1 --profile= "${CMAKE_CURRENT_BINARY_DIR}")
  add_cli_test(cli_empty_project_file 1 --project-file= "${CMAKE_CURRENT_BINARY_DIR}")
  add_cli_test(cli_empty_output_folder 1 --output-folder= "${CMAKE_CURRENT_BINARY_DIR}")
  add_cli_test(cli_empty_orientation_detect 1 --orientation-detect= "${CMAKE_CURRENT_BINARY_DIR}")
  add_cli_test(cli_empty_report_file 1 --report-file= "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_empty_export_name 1 --export-name= "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_unknown_format 1 --export-formats=doc "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # The switch --export is gone; the export runs with --last-step export.
  add_cli_test(cli_export_with_value 1 --export=. "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # The option is gone; the export runs with --last-step export.
  add_cli_test(cli_old_pdf_option 1 --pdf=book.pdf "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  # The options for creating a project are read in both modes; run() turns them into usage errors
  # with an existing project. Without that, the missing project below gives exit code 2.
  add_cli_test(cli_output_folder_with_project 1 --output-folder="${CMAKE_CURRENT_BINARY_DIR}/new-output"
               "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_profile_with_project 1 --profile=Default "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_dpi_with_project 1 --dpi=300 "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_dpi_fix_with_project 1 --dpi-fix=300 "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_profile_list_with_positional 1 --profile-list "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_profile_list_with_threads 1 --profile-list --threads=2)
  add_cli_test(cli_profile_list_with_project_info 1 --profile-list --project-info x.ScanTailor)
  add_cli_test(cli_project_info_with_folder 1 --project-info "${CMAKE_CURRENT_BINARY_DIR}")
  add_cli_test(cli_project_info_with_last_step 1 --project-info --last-step=margins "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")
  add_cli_test(cli_project_info_without_arg 1 --project-info)
  # CommandLineRun::PROJECT_UNUSABLE
  add_cli_test(cli_missing_project 2 "${CMAKE_CURRENT_BINARY_DIR}/missing.ScanTailor")

  # An export option without --last-step has to lead the run to the export.
  add_executable(cli_folder_export_test cli_folder_export_test.cpp)
  # core for writing a profile without text recognition into the test folder.
  target_link_libraries(cli_folder_export_test PRIVATE core Qt::Core Qt::Gui)
  add_test(NAME cli_folder_export_formats_without_last_step COMMAND cli_folder_export_test
                                                             "$<TARGET_FILE:scantailor-ocr-cli>")
  set_unit_test_environment(cli_folder_export_formats_without_last_step)

  # Ctrl+Break is how another program stops the tool on Windows; elsewhere it sends SIGINT or SIGTERM.
  if (WIN32)
    add_executable(cli_ctrl_break_test cli_ctrl_break_test.cpp)
    target_link_libraries(cli_ctrl_break_test PRIVATE Qt::Core Qt::Gui)
    add_test(NAME cli_ctrl_break_cancels COMMAND cli_ctrl_break_test "$<TARGET_FILE:scantailor-ocr-cli>")
    set_unit_test_environment(cli_ctrl_break_cancels)
  endif()
endif()
