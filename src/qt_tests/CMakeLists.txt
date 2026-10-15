set(sources
    main.cpp
    tst_core_qt.cpp
    tst_ux_qt.cpp)

add_executable(qt_tests ${sources})
target_link_libraries(
    qt_tests
    PRIVATE core dewarping foundation ${Qt_Test_lib} ${Qt_Core_lib} ${Qt_Gui_lib} ${Qt_Widgets_lib} ${EXTRA_LIBS})
# The tests of the output options need the header that uic generates for them.
target_include_directories(qt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src"
                                            "$<TARGET_PROPERTY:output,BINARY_DIR>/output_autogen/include")

add_test(NAME qt_tests COMMAND qt_tests)
