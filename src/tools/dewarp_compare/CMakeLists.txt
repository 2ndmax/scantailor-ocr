add_executable(dewarp_compare main.cpp)
target_link_libraries(dewarp_compare PRIVATE dewarping core ${EXTRA_LIBS})
