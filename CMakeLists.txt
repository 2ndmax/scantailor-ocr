cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(target_name scantailor-ocr)

# Disable in-source builds.
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR
      "FATAL: In-source builds are not allowed."
      " You should create a separate directory for build files.")
endif()

# Set cmake policies.
if (POLICY CMP0071)
  cmake_policy(SET CMP0071 NEW)
endif()
# find_package(Boost) uses the BoostConfig.cmake shipped with Boost (1.70+)
# instead of CMake's removed FindBoost module.
if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
# install() DESTINATION paths are normalized.
if (POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

project("ScanTailor OCR")

# Set compiler flags.
set(CMAKE_CXX_STANDARD 17)

if (NOT DEFAULT_COMPILER_FLAGS_IS_SET)
  # Note for MSVC: _SECURE_SCL / _ITERATOR_DEBUG_LEVEL must NOT be overridden here.
  # It changes the ABI of the standard containers, so a mismatch with the
  # prebuilt Qt and Boost binaries is an ODR violation that leads to crashes
  # at library boundaries. (_SECURE_SCL is also deprecated since VS2012.)

  if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}"
      CACHE STRING "Common C flags for all build configurations." FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}"
      CACHE STRING "Common C++ flags for all build configurations." FORCE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}"
      CACHE STRING "Common link flags for all build configurations." FORCE)

  set(DEFAULT_COMPILER_FLAGS_IS_SET YES CACHE INTERNAL "" FORCE)
endif()

# Off by default: -march=native produces a binary that only runs on CPUs
# compatible with the build machine. Anything meant to be distributed
# (distro packages, AppImage, CI artifacts) would abort with SIGILL elsewhere.
# Kept outside the block above, as add_compile_options() sets a directory
# property that has to be applied on every configure run, not just the first.
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_COMPILER_IS_GNUCXX)
  option(NATIVE_ARCH "Optimize for the build machine's CPU. The resulting binary is not portable." OFF)
  if (NATIVE_ARCH)
    add_compile_options("$<$<CONFIG:RELEASE>:-march=native;-mtune=native>")
  endif()
endif()

# Extra diagnostics for finding bugs: high warning level plus the static
# code analysis of the compiler. Slows the build down considerably, so it's
# best used in a separate build directory.
option(ENABLE_CODE_ANALYSIS "Enable extra warnings and the compiler's static code analysis (MSVC: /W4 /analyze)." OFF)
if (ENABLE_CODE_ANALYSIS)
  if (MSVC)
    # Headers of the dependencies (Qt, Boost, libtiff, ...) are included as
    # external headers, whose warnings and analysis results are suppressed.
    add_compile_options(/W4 /analyze /analyze:external- /external:W0 /external:anglebrackets)
    # Not reported, both are intended: C4324 (structure padded because of an alignment
    # specifier: the aligned jmp_buf in the JPEG error managers) and C4250 (inheriting
    # via dominance, as the output::OutputImage* classes do on purpose).
    add_compile_options(/wd4324 /wd4250)
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
  endif()
endif()

if (UNIX)
  option(CMAKE_POSITION_INDEPENDENT_CODE "" ON)
  option(CMAKE_INSTALL_RPATH_USE_LINK_PATH "" ON)
endif()

include(cmake/SetDefaultBuildType.cmake)
include(cmake/UpdateTranslations.cmake)
include(cmake/ListItemsPrepend.cmake)
include(cmake/ColorSchemes.cmake)
if (WIN32)
  include(cmake/CopyToBuildDir.cmake)
  include(cmake/AddDynamicLibraryLocations.cmake)
endif()

st_set_default_build_type(Release)

# Must be set before Boost (distribution packages and other chroot builds use -DBUILD_TESTS=OFF; issue #61).
option(BUILD_TESTS "Build unit tests (math_tests, imageproc_tests, core_tests, foundation_tests, qt_tests)" ON)
option(BUILD_DEV_TOOLS "Build developer tools (binarization_compare: binarization methods; color_detect: color mode detection on sample pages; despeckle_compare: despeckling before and after a change; split_compare: split line detection methods; oblique_compare: oblique detection on synthetic pages or real scans)" OFF)

# Fuzz targets in src/fuzz, for a build directory of their own (see TESTING.md). All of the
# project's code is built with AddressSanitizer and the coverage instrumentation libFuzzer
# needs. MSVC can't link such code with uninstrumented code (LNK2038 annotate_vector ...), and
# the instrumented code needs the libFuzzer runtime, so the program, the tests and the dev
# tools are left out; the tests under sanitizers are scripts/analysis/08-sanitizers.ps1.
option(BUILD_FUZZERS "Build the fuzz targets (src/fuzz) instead of the program, with AddressSanitizer. MSVC only." OFF)
if (BUILD_FUZZERS)
  if (NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "BUILD_FUZZERS needs MSVC (cl.exe).")
  endif()
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_DEV_TOOLS OFF CACHE BOOL "" FORCE)
  add_compile_options(/fsanitize=address /fsanitize-coverage=inline-8bit-counters /fsanitize-coverage=edge
                      /fsanitize-coverage=trace-cmp /fsanitize-coverage=trace-div)
  # ASan doesn't work with incremental linking.
  add_link_options(/INCREMENTAL:NO)
endif()

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
if (NOT CMAKE_INSTALL_BINDIR)
  if (WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
  else()
    set(CMAKE_INSTALL_BINDIR "bin")
  endif()
endif()

#=================================== JPEG ===================================#

find_package(JPEG REQUIRED)

if (WIN32)
  add_dynamic_library_locations(JPEG::JPEG "libjpeg-62.dll")
endif()

#=================================== ZLIB ===================================#

find_package(ZLIB REQUIRED)

if (WIN32)
  add_dynamic_library_locations(ZLIB::ZLIB)
endif()

#================================== PNG ==================================#

find_package(PNG REQUIRED)

if (WIN32)
  add_dynamic_library_locations(PNG::PNG "libpng16.dll")
endif()

#=================================== TIFF ===================================#

find_package(TIFF REQUIRED)

if (WIN32)
  add_dynamic_library_locations(TIFF::TIFF)
  set_target_properties(
      TIFF::TIFF PROPERTIES
      INTERFACE_COMPILE_DEFINITIONS USE_LIBTIFF_DLL)
endif()

# The compression schemes libtiff supports are decided when libtiff itself is
# built. A libtiff lacking a codec silently fails to open files using it.
set(TIFF_REQUIRED_CODECS "LZW;PACKBITS;CCITT;JPEG;OJPEG;DEFLATE;LZMA" CACHE STRING
    "TIFF compression schemes libtiff has to support (see cmake/CheckTiffCodecs.cmake).")
set(TIFF_RECOMMENDED_CODECS "ZSTD;WEBP;LERC" CACHE STRING
    "TIFF compression schemes libtiff should support; only a warning is issued if it doesn't.")
option(SKIP_TIFF_CODEC_CHECK "Don't verify the compression schemes supported by libtiff." OFF)
include(cmake/CheckTiffCodecs.cmake)
check_tiff_codecs(REQUIRED ${TIFF_REQUIRED_CODECS} RECOMMENDED ${TIFF_RECOMMENDED_CODECS})

#================================= OpenJPEG ==================================#

# JPEG 2000 support.
find_package(OpenJPEG CONFIG QUIET)
if (OpenJPEG_FOUND AND TARGET openjp2)
  set(OPENJPEG_TARGET openjp2)
  if (OPENJPEG_INCLUDE_DIRS)
    # Older OpenJPEG config files don't attach the include directories to the target.
    set_property(TARGET openjp2 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${OPENJPEG_INCLUDE_DIRS})
  endif()
  message(STATUS "Found OpenJPEG: ${OPENJPEG_MAJOR_VERSION}.${OPENJPEG_MINOR_VERSION}.${OPENJPEG_BUILD_VERSION}")
else()
  find_package(PkgConfig QUIET)
  if (PKG_CONFIG_FOUND)
    pkg_check_modules(OPENJP2 QUIET IMPORTED_TARGET libopenjp2>=2.3)
  endif()
  if (OPENJP2_FOUND)
    set(OPENJPEG_TARGET PkgConfig::OPENJP2)
    message(STATUS "Found OpenJPEG (pkg-config): ${OPENJP2_VERSION}")
  else()
    message(FATAL_ERROR
        "OpenJPEG (2.3 or newer) is required for JPEG 2000 support.\n"
        "With vcpkg: vcpkg install openjpeg\n"
        "On Debian / Ubuntu: apt install libopenjp2-7-dev")
  endif()
endif()

#================================= Tesseract =================================#

# Text recognition (OCR) for the PDF export.
option(ENABLE_OCR "Text recognition (OCR) for the PDF export, using Tesseract." ON)
set(TESSERACT_TARGET "")
if (ENABLE_OCR)
  find_package(Tesseract CONFIG QUIET)
  if (Tesseract_FOUND AND TARGET Tesseract::libtesseract)
    set(TESSERACT_TARGET Tesseract::libtesseract)
    message(STATUS "Found Tesseract: ${Tesseract_VERSION}")
  else()
    # Debian / Ubuntu ship no CMake config files for Tesseract, only pkg-config ones.
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
      pkg_check_modules(TESSERACT QUIET IMPORTED_TARGET tesseract>=4.1 lept)
    endif()
    if (TESSERACT_FOUND)
      set(TESSERACT_TARGET PkgConfig::TESSERACT)
      message(STATUS "Found Tesseract (pkg-config): ${TESSERACT_tesseract_VERSION}")
    else()
      message(FATAL_ERROR
          "Tesseract (4.1 or newer) is required for text recognition (OCR) in the PDF export.\n"
          "With vcpkg: vcpkg install tesseract\n"
          "On Debian / Ubuntu: apt install libtesseract-dev libleptonica-dev\n"
          "To build without text recognition, configure with -DENABLE_OCR=OFF.")
    endif()
  endif()
else()
  message(STATUS "Text recognition (OCR) is disabled.")
endif()

#================================= Boost ================================#

set(Boost_USE_MULTITHREADED ON)

if (BUILD_TESTS)
  find_package(Boost 1.60 COMPONENTS unit_test_framework REQUIRED)
else()
  find_package(Boost 1.60 REQUIRED)
endif()

include_directories(${Boost_INCLUDE_DIRS})
add_definitions(-DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION)
if (BUILD_TESTS)
  if (NOT Boost_USE_STATIC_LIBS)
    if (WIN32)
      add_dynamic_library_locations(Boost::unit_test_framework)
    endif()
    add_definitions(-DBOOST_TEST_DYN_LINK)
  endif()
endif()

#=================================== Qt ===================================#

# Qt 6.4 is the version of Ubuntu 24.04.
set(qt_min_version 6.4)
# Functions deprecated up to that version are not available, so new code can't use them.
add_compile_definitions(QT_DISABLE_DEPRECATED_UP_TO=0x060400)

set(qt_components Core Gui Widgets Xml Network LinguistTools OpenGL Svg OpenGLWidgets)
if (BUILD_TESTS)
  list(APPEND qt_components Test)
endif()
find_package(Qt6 ${qt_min_version} COMPONENTS ${qt_components} CONFIG REQUIRED)
set(QT_BINDIR "${QT6_INSTALL_PREFIX}/bin")


#=================================== Threads ===================================#

if (UNIX)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  link_libraries(Threads::Threads)
endif()

#=================================== System ===================================#

set(EXTRA_LIBS "")
if (WIN32)
  list(APPEND EXTRA_LIBS winmm imm32 ws2_32 ole32 oleaut32 uuid gdi32 comdlg32 winspool)
endif()

#=================================== Main ===================================#

set(CMAKE_AUTOMOC ON)

if (WIN32 AND MSVC)
  add_definitions(-DNOMINMAX)
endif()

if (BUILD_TESTS)
  enable_testing()
endif()

# Prepare config.h
option(PORTABLE_VERSION "Whether to build the portable version." ON)
if (PORTABLE_VERSION)
  set(PORTABLE_CONFIG_DIR "config")
endif()
set(APPLICATION_NAME "${target_name}")
if (WIN32)
  set(TRANSLATION_DIRS ".:translations")
  set(PLUGIN_DIRS ".")
else()
  set(TRANSLATION_DIRS ".:translations:../share/${APPLICATION_NAME}/translations")
  set(PLUGIN_DIRS ".:../lib/${APPLICATION_NAME}")
endif()
configure_file(config.h.in "${CMAKE_BINARY_DIR}/config.h.tmp" @ONLY)
file(READ "${CMAKE_BINARY_DIR}/config.h.tmp" config_h_contents)
set(enable_developer_version_default OFF)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(enable_developer_version_default ON)
endif()
option(DEVELOPER_VERSION "Whether to enable the internal debug features." ${enable_developer_version_default})
if (NOT DEVELOPER_VERSION)
  string(REGEX REPLACE "(#define[ \\t]+ENABLE_DEBUG_FEATURES[^\n]*)" "//\\1" config_h_contents ${config_h_contents})
endif()
file(WRITE "${CMAKE_BINARY_DIR}/config.h.tmp" ${config_h_contents})
# Unlike file(WRITE), configure_file() leaves config.h alone if nothing changed, so
# configuring again doesn't rebuild everything that includes it.
configure_file("${CMAKE_BINARY_DIR}/config.h.tmp" "${CMAKE_BINARY_DIR}/config.h" COPYONLY)
include_directories(BEFORE "${CMAKE_BINARY_DIR}")

# Extract VERSION and VERSION_QUAD from version.h
file(READ "${PROJECT_SOURCE_DIR}/version.h.in" version_h_contents)
string(
    REGEX REPLACE
    ".*#define[ \\t]+VERSION[ \\t]+\"([^\"]*)\".*"
    "\\1" VERSION "${version_h_contents}")
if ("${VERSION}" STREQUAL "${version_h_contents}")
  message(FATAL_ERROR "Failed to extract VERSION from version.h")
endif()
# VERSION_QUAD must be either empty or be in the form of X.Y.Z.Y
string(
    REGEX REPLACE
    ".*#define[ \\t]+VERSION_QUAD[ \\t]+\"(([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)?)\".*"
    "\\1" VERSION_QUAD "${version_h_contents}")
if ("${VERSION_QUAD}" STREQUAL "${version_h_contents}")
  message(FATAL_ERROR "Failed to extract VERSION_QUAD from version.h")
endif()
configure_file(version.h.in "${CMAKE_BINARY_DIR}/version.h" COPYONLY)
# Only the numbers: a suffix like "-rc1" would make the Windows FILEVERSION invalid.
if (NOT VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "VERSION in version.h has to start with X.Y.Z: ${VERSION}")
endif()
set(APPLICATION_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(APPLICATION_VERSION_MINOR "${CMAKE_MATCH_2}")
set(APPLICATION_VERSION_PATCH "${CMAKE_MATCH_3}")

set(SCANTAILOR_RESOURCES_DIR "${CMAKE_SOURCE_DIR}/src/resources")
set(SCANTAILOR_TRANSLATIONS_DIR "${CMAKE_SOURCE_DIR}/src/translations")

# Before src/: the core includes the color headers, the application the resources.
set(color_scheme_qrc_files "")
foreach(scheme light dark)
  generate_color_scheme(${scheme} scheme_qrc)
  list(APPEND color_scheme_qrc_files "${scheme_qrc}")
endforeach()

configure_file(
    "${SCANTAILOR_RESOURCES_DIR}/unix/${target_name}.desktop.in"
    "${CMAKE_BINARY_DIR}/${target_name}.desktop"
    @ONLY)

# translation_sources() appends to these cache lists; start empty on every configure run,
# or deleted source files stay in the lupdate list and make update_translations fail.
unset(scantailor_TRANSLATION_SOURCES CACHE)
unset(scantailor_TRANSLATION_INCLUDE_DIRS CACHE)

add_subdirectory(src)

# Translations
file(GLOB translation_files "${SCANTAILOR_TRANSLATIONS_DIR}/${target_name}_*.ts")

finalize_translations(scantailor ${translation_files})
update_translations_target(update_translations scantailor)

list(FILTER translation_files EXCLUDE REGEX ".*untranslated.ts")
Qt_add_translation(qm_files ${translation_files})
add_custom_target(compile_translations ALL DEPENDS ${qm_files})
if (WIN32)
  install(FILES ${qm_files} DESTINATION "${CMAKE_INSTALL_BINDIR}/translations")
else()
  install(FILES ${qm_files} DESTINATION "share/${APPLICATION_NAME}/translations")
endif()

# Install extra files.
if (WIN32)
  macro (add_runtime_libs_to_install Configuration Libs)
    set(configurations "${Configuration}")
    if (${configurations} MATCHES "ALL")
      set(configurations "DEBUG;RELEASE")
    endif()
    foreach (_config ${configurations})
      foreach (_lib ${Libs})
        if (EXISTS "${_lib}")
          list(APPEND "ADDITIONAL_RUNTIME_LIBS_${_config}" "${_lib}")
        endif()
      endforeach()
    endforeach()
  endmacro()

  macro (set_if_undefined Var Value)
    if (NOT ${Var})
      set(${Var} "${Value}")
    endif()
  endmacro()

  # Install the DLLs of the dependencies. The list is not complete (Tesseract and
  # Leptonica, the TIFF codecs and the Qt plugins are missing); the Windows ZIP is made
  # with scripts/release/package-windows.ps1, which collects everything.
  set(dep_libs
      JPEG::JPEG PNG::PNG ZLIB::ZLIB TIFF::TIFF ${OPENJPEG_TARGET}
      Qt::Core Qt::Gui Qt::Widgets Qt::Xml
      Qt::Network Qt::OpenGL Qt::Svg Qt::OpenGLWidgets)

  foreach (_target ${dep_libs})
    get_target_property(target_type ${_target} TYPE)
    if (target_type STREQUAL "SHARED_LIBRARY")
      set(dynlib_location_property LOCATION)
    elseif (target_type STREQUAL "UNKNOWN_LIBRARY")
      set(dynlib_location_property IMPORTED_DYNLIB)
    else()
      continue()
    endif()

    get_target_property(release_loc ${_target} ${dynlib_location_property}_RELEASE)
    get_target_property(debug_loc ${_target} ${dynlib_location_property}_DEBUG)
    set_if_undefined(debug_loc "${release_loc}")
    add_runtime_libs_to_install(RELEASE "${release_loc}")
    add_runtime_libs_to_install(DEBUG "${debug_loc}")
  endforeach()

  # Install MinGW runtime components.
  if (MINGW)
    get_filename_component(_mingw_path ${CMAKE_CXX_COMPILER} PATH)
    file(GLOB libgcc_s "${_mingw_path}/libgcc_s_*.dll")
    file(GLOB libstdcpp "${_mingw_path}/libstdc++*.dll")
    file(GLOB libwinpthread "${_mingw_path}/libwinpthread*.dll")
    add_runtime_libs_to_install(ALL "${libgcc_s};${libstdcpp};${libwinpthread}")
  endif()

  copy_to_build_dir(${ADDITIONAL_RUNTIME_LIBS_RELEASE} CONFIGURATIONS Release MinSizeRel RelWithDebInfo)
  copy_to_build_dir(${ADDITIONAL_RUNTIME_LIBS_DEBUG} CONFIGURATIONS Debug)

  # Generate the target that will actually do the copying.
  if (BUILD_FUZZERS)
    # The fuzz targets also need the AddressSanitizer runtime, which comes with the compiler.
    get_filename_component(compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    copy_to_build_dir("${compiler_dir}/clang_rt.asan_dynamic-x86_64.dll")
    generate_copy_to_build_dir_target(copy_to_build_dir fuzz_image_metadata)
  else()
    generate_copy_to_build_dir_target(copy_to_build_dir ${target_name})
  endif()

  install(PROGRAMS ${ADDITIONAL_RUNTIME_LIBS_RELEASE} TYPE BIN CONFIGURATIONS Release MinSizeRel RelWithDebInfo)
  install(PROGRAMS ${ADDITIONAL_RUNTIME_LIBS_DEBUG} TYPE BIN CONFIGURATIONS Debug)
endif()

if (UNIX)
  install(FILES "${CMAKE_BINARY_DIR}/${target_name}.desktop"
      DESTINATION "share/applications")
  install(FILES "${SCANTAILOR_RESOURCES_DIR}/appicon.svg"
      DESTINATION "share/icons/hicolor/scalable/apps"
      RENAME "${target_name}.svg")
  # Renamed so the package can be installed next to ScanTailor Advanced, which has the same file.
  install(FILES "${SCANTAILOR_RESOURCES_DIR}/unix/mime/scantailor-project.xml"
      DESTINATION "share/mime/packages"
      RENAME "${target_name}-project.xml")
endif()

if (WIN32 AND MSVC)
  set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
endif()
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
include(InstallRequiredSystemLibraries)

# Packaging
set(CPACK_PACKAGE_NAME "${APPLICATION_NAME}")
set(CPACK_PACKAGE_VERSION_MAJOR "${APPLICATION_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${APPLICATION_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${APPLICATION_VERSION_PATCH}")
set(CPACK_PACKAGE_VENDOR "ScanTailor OCR <https://github.com/2ndmax/scantailor-ocr>")
set(CPACK_PACKAGE_CONTACT "${CPACK_PACKAGE_VENDOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Post-processing of scanned pages with export to searchable PDF (OCR).")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES "${target_name};${CMAKE_PROJECT_NAME}")
set(CPACK_CREATE_DESKTOP_LINKS "${target_name}")
if (WIN32)
  set(CPACK_NSIS_INSTALLED_ICON_NAME "${target_name}.exe")
  set(CPACK_NSIS_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
  set(CPACK_NSIS_DISPLAY_NAME "${CMAKE_PROJECT_NAME} ${VERSION}")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_BINARY_DIR}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_NAME}")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
      "CreateShortCut \\\"$DESKTOP\\\\${CMAKE_PROJECT_NAME}.lnk\\\" \\\"$INSTDIR\\\\${target_name}.exe\\\"")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
      "Delete \\\"$DESKTOP\\\\${CMAKE_PROJECT_NAME}.lnk\\\"")
endif()
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${APPLICATION_NAME}-${VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.svn/"
    "/\\\\.git/"
    "~$"
    "\\\\.pcs$"
    "TODO.txt"
    "CMakeLists.txt.user"
    "/doxygen/"
    "${CMAKE_BINARY_DIR}")
include(CPack)

# uninstall target
if (NOT TARGET uninstall)
  configure_file(
      "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
      "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
      @ONLY)
  add_custom_target(
      uninstall
      COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")
endif()
