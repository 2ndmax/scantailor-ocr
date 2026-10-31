# Fuzz targets for libFuzzer, built only with BUILD_FUZZERS (see the top-level CMakeLists.txt).
# Each target is one program; FuzzSetup.cpp is compiled into every one of them, as it defines
# LLVMFuzzerInitialize, which libFuzzer would not pull out of a static library.
# The seed files are in corpus/, scripts/analysis/10-fuzz.ps1 runs the targets.

function(add_fuzzer name)
  add_executable(${name} ${ARGN} FuzzSetup.cpp FuzzSetup.h)
  # Adds the libFuzzer runtime with its main(); the libraries only have the instrumentation.
  target_compile_options(${name} PRIVATE /fsanitize=fuzzer)
  target_link_libraries(${name} PRIVATE core dewarping)
endfunction()

add_fuzzer(fuzz_image_metadata FuzzImageMetadata.cpp)
add_fuzzer(fuzz_project_reader FuzzProjectReader.cpp)
add_fuzzer(fuzz_filter_params FuzzFilterParams.cpp)
add_fuzzer(fuzz_image_loader FuzzImageLoader.cpp)
add_fuzzer(fuzz_image_processing FuzzImageProcessing.cpp)
add_fuzzer(fuzz_profile_reader FuzzProfileReader.cpp)
add_fuzzer(fuzz_page_output FuzzPageOutput.cpp)
add_fuzzer(fuzz_project_geometry FuzzProjectGeometry.cpp)
