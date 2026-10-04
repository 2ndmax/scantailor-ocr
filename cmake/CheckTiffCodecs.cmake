# check_tiff_codecs(REQUIRED <codec>... [RECOMMENDED <codec>...])
#
# Verifies that the libtiff found by find_package(TIFF) supports the given
# compression schemes.  Which codecs libtiff supports is decided when libtiff
# itself is built, and it can only be queried at run time.  So a small program
# is built against TIFF::TIFF, which asks TIFFIsCODECConfigured() about each codec.
#
# Known codec names: LZW PACKBITS CCITT JPEG OJPEG DEFLATE LZMA ZSTD WEBP LERC JBIG
#
# Missing REQUIRED codecs are a fatal error, missing RECOMMENDED ones a warning.
# The check is skipped when cross-compiling, or when SKIP_TIFF_CODEC_CHECK is ON.

function(check_tiff_codecs)
  cmake_parse_arguments(ARG "" "" "REQUIRED;RECOMMENDED" ${ARGN})

  if (SKIP_TIFF_CODEC_CHECK)
    message(STATUS "Checking libtiff codecs - skipped (SKIP_TIFF_CODEC_CHECK=ON)")
    return()
  endif()
  if (CMAKE_CROSSCOMPILING)
    message(WARNING "Cross-compiling: can't verify that libtiff supports these codecs: ${ARG_REQUIRED}")
    return()
  endif()

  set(check_dir "${CMAKE_BINARY_DIR}/CMakeFiles/CheckTiffCodecs")
  set(source_file "${check_dir}/check_tiff_codecs.c")
  set(executable "${check_dir}/check_tiff_codecs${CMAKE_EXECUTABLE_SUFFIX}")
  file(WRITE "${source_file}" [=[
#include <stdio.h>
#include <tiffio.h>

int main(void) {
  /* Numeric ids, so this also compiles against old headers lacking the newer constants. */
  static const struct {
    const char* name;
    unsigned short id;
  } codecs[] = {
      {"LZW", 5},     {"PACKBITS", 32773}, {"CCITT", 4},     {"JPEG", 7},      {"OJPEG", 6},    {"DEFLATE", 32946},
      {"LZMA", 34925}, {"ZSTD", 50000},    {"WEBP", 50001}, {"LERC", 34887}, {"JBIG", 34661}};
  size_t i;
  for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); ++i) {
    printf("%s=%d\n", codecs[i].name, TIFFIsCODECConfigured(codecs[i].id) ? 1 : 0);
  }
  return 0;
}
]=])

  # Multi-config generators would otherwise build the check in Debug mode,
  # linking against debug libraries whose DLLs aren't in the search path.
  set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  try_compile(
      compiled "${check_dir}/build"
      SOURCES "${source_file}"
      LINK_LIBRARIES TIFF::TIFF
      COPY_FILE "${executable}"
      OUTPUT_VARIABLE build_output)
  if (NOT compiled)
    message(WARNING "Could not build the libtiff codec check, skipping it:\n${build_output}")
    return()
  endif()

  # On Windows, the DLLs of libtiff and its codecs have to be found at run time.
  set(saved_path "$ENV{PATH}")
  if (WIN32)
    set(dll_dirs)
    foreach (_config RELEASE RELWITHDEBINFO MINSIZEREL NOCONFIG)
      get_target_property(_dll TIFF::TIFF IMPORTED_DYNLIB_${_config})
      if (_dll)
        get_filename_component(_dll_dir "${_dll}" DIRECTORY)
        list(APPEND dll_dirs "${_dll_dir}")
      endif()
    endforeach()
    if (DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
      list(APPEND dll_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    foreach (_dir ${dll_dirs})
      file(TO_NATIVE_PATH "${_dir}" _native_dir)
      set(ENV{PATH} "${_native_dir};$ENV{PATH}")
    endforeach()
  endif()

  execute_process(
      COMMAND "${executable}"
      RESULT_VARIABLE run_result
      OUTPUT_VARIABLE run_output
      ERROR_VARIABLE run_error)
  set(ENV{PATH} "${saved_path}")

  if (NOT run_result EQUAL 0)
    message(WARNING
        "Could not run the libtiff codec check (${run_result}), skipping it. ${run_error}\n"
        "Make sure the libtiff built into the program supports these codecs: ${ARG_REQUIRED}")
    return()
  endif()

  set(supported)
  set(missing_required)
  set(missing_recommended)
  foreach (_codec ${ARG_REQUIRED} ${ARG_RECOMMENDED})
    string(TOUPPER "${_codec}" _codec)
    if (NOT run_output MATCHES "(^|\n)${_codec}=")
      message(FATAL_ERROR "check_tiff_codecs(): unknown codec name \"${_codec}\"")
    endif()
  endforeach()
  string(REGEX MATCHALL "[A-Z]+=1" _found "${run_output}")
  foreach (_entry ${_found})
    string(REPLACE "=1" "" _codec "${_entry}")
    list(APPEND supported "${_codec}")
  endforeach()
  foreach (_codec ${ARG_REQUIRED})
    string(TOUPPER "${_codec}" _codec)
    if (NOT _codec IN_LIST supported)
      list(APPEND missing_required "${_codec}")
    endif()
  endforeach()
  foreach (_codec ${ARG_RECOMMENDED})
    string(TOUPPER "${_codec}" _codec)
    if (NOT _codec IN_LIST supported)
      list(APPEND missing_recommended "${_codec}")
    endif()
  endforeach()

  string(REPLACE ";" " " _supported_str "${supported}")
  message(STATUS "libtiff supports these codecs: ${_supported_str}")

  if (missing_recommended)
    string(REPLACE ";" " " _str "${missing_recommended}")
    message(WARNING
        "libtiff lacks support for these recommended codecs: ${_str}\n"
        "TIFF files using them can't be opened.  With vcpkg, add the corresponding features, e.g.:\n"
        "  vcpkg install tiff[core,jpeg,zip,lzma,zstd,webp,lerc]")
  endif()
  if (missing_required)
    string(REPLACE ";" " " _str "${missing_required}")
    message(FATAL_ERROR
        "libtiff lacks support for these required codecs: ${_str}\n"
        "Please use a libtiff built with them.  With vcpkg, e.g.:\n"
        "  vcpkg install tiff[core,jpeg,zip,lzma,zstd,webp,lerc]\n"
        "On Debian / Ubuntu, the libtiff-dev package has everything that's needed.\n"
        "To build anyway, pass -DSKIP_TIFF_CODEC_CHECK=ON (not recommended).")
  endif()
endfunction()
