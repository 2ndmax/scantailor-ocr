add_executable(despeckle_compare main.cpp Bench.cpp SpotSheet.cpp)
target_link_libraries(despeckle_compare PRIVATE core ${EXTRA_LIBS})
# Bench.cpp holds German text.
if(MSVC)
  target_compile_options(despeckle_compare PRIVATE /utf-8)
endif()
