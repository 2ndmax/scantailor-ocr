set(sources
    main.cpp
    tst_core_qt.cpp
    tst_deskew_qt.cpp
    tst_end_to_end_qt.cpp
    tst_fix_orientation_qt.cpp
    tst_ux_qt.cpp)

add_executable(qt_tests ${sources})
target_link_libraries(
    qt_tests
    PRIVATE core dewarping foundation Qt::Test Qt::Core Qt::Gui Qt::Widgets TIFF::TIFF ${EXTRA_LIBS})
target_include_directories(qt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
# The tests of the options widgets need the headers that uic generates for them. These have the same
# names in each filter, so each test file only gets the directory of its filter.
set_source_files_properties(tst_core_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                                                       "$<TARGET_PROPERTY:output,BINARY_DIR>/output_autogen/include")
set_source_files_properties(tst_deskew_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                                                         "$<TARGET_PROPERTY:deskew,BINARY_DIR>/deskew_autogen/include")
set_source_files_properties(
  tst_fix_orientation_qt.cpp PROPERTIES INCLUDE_DIRECTORIES
                             "$<TARGET_PROPERTY:fix_orientation,BINARY_DIR>/fix_orientation_autogen/include")

add_test(NAME qt_tests COMMAND qt_tests)
set_unit_test_environment(qt_tests)
