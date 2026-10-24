set(sources
    main.cpp
    TestDewarping.cpp)

add_executable(dewarping_tests ${sources})
target_link_libraries(
    dewarping_tests
    PRIVATE dewarping Boost::unit_test_framework ${EXTRA_LIBS})

add_test(NAME dewarping_tests COMMAND dewarping_tests --log_level=message)
set_unit_test_environment(dewarping_tests)