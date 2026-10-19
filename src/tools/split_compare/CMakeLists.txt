add_executable(split_compare main.cpp)
target_link_libraries(split_compare PRIVATE page_split core ${EXTRA_LIBS})
