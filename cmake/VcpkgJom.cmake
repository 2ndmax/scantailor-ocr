# Uses the jom.exe that vcpkg downloads (vcpkg_find_acquire_program(JOM)) as the
# make program for the "NMake Makefiles JOM" generator.
#
# vcpkg_find_acquire_program() itself is only available inside vcpkg portfiles,
# so this looks in the same place it unpacks to: <downloads>/tools/jom/jom-<version>.
# Must be included before project(), which is where CMake checks the make program.

if (CMAKE_GENERATOR STREQUAL "NMake Makefiles JOM" AND NOT EXISTS "${CMAKE_MAKE_PROGRAM}")
  set(_st_vcpkg_downloads "")
  if (DEFINED ENV{VCPKG_DOWNLOADS})
    list(APPEND _st_vcpkg_downloads "$ENV{VCPKG_DOWNLOADS}")
  endif()
  if (CMAKE_TOOLCHAIN_FILE MATCHES "scripts[/\\\\]buildsystems[/\\\\]vcpkg\\.cmake$")
    get_filename_component(_st_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}/../../.." ABSOLUTE)
    list(APPEND _st_vcpkg_downloads "${_st_vcpkg_root}/downloads")
  endif()
  if (DEFINED ENV{VCPKG_ROOT})
    list(APPEND _st_vcpkg_downloads "$ENV{VCPKG_ROOT}/downloads")
  endif()

  set(_st_jom_dirs "")
  foreach (_st_dir IN LISTS _st_vcpkg_downloads)
    file(GLOB _st_found LIST_DIRECTORIES true "${_st_dir}/tools/jom/jom-*")
    list(APPEND _st_jom_dirs ${_st_found})
  endforeach()
  # Prefer the newest version.
  list(SORT _st_jom_dirs COMPARE NATURAL ORDER DESCENDING)

  find_program(VCPKG_JOM_EXECUTABLE jom HINTS ${_st_jom_dirs} NO_DEFAULT_PATH)
  if (VCPKG_JOM_EXECUTABLE)
    set(CMAKE_MAKE_PROGRAM "${VCPKG_JOM_EXECUTABLE}" CACHE FILEPATH "Path to a program." FORCE)
    message(STATUS "Using jom from vcpkg: ${CMAKE_MAKE_PROGRAM}")
  else()
    message(STATUS "jom not found in the vcpkg downloads; falling back to the PATH.")
  endif()

  unset(_st_vcpkg_downloads)
  unset(_st_vcpkg_root)
  unset(_st_jom_dirs)
  unset(_st_dir)
  unset(_st_found)
endif()
