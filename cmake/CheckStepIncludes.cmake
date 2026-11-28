# Checks that no step includes the headers of a step it doesn't link.
#
# All steps share the include root src/core/filters, so the compiler finds every step's headers from
# every step, and core links all steps, so the linker doesn't notice either. A step that included a
# step before it in the chain (e.g. deskew including "page_split/Task.h") would bring back a hidden
# dependency cycle. The allowed steps come from the steps' own target_link_libraries, so the chain is
# written down in one place only.
#
# Called by ctest (src/core/tests/CMakeLists.txt) with
#   -DFILTERS_DIR=<src/core/filters>
#   -DSTEPS=<step>|<step>|...                   all step libraries
#   -DLINKS_<step>=<library>|<library>|...      the step's LINK_LIBRARIES
#   -DSCANTAILOR_CMAKE_MINIMUM_VERSION=<the project's minimum version>

# A script run with -P doesn't inherit the project's policies; without this, CMake 3.x treats
# IN_LIST as an unknown argument (policy CMP0057).
if (NOT DEFINED SCANTAILOR_CMAKE_MINIMUM_VERSION)
  message(FATAL_ERROR "CheckStepIncludes.cmake needs -DSCANTAILOR_CMAKE_MINIMUM_VERSION=...")
endif()
cmake_minimum_required(VERSION ${SCANTAILOR_CMAKE_MINIMUM_VERSION})

foreach (var FILTERS_DIR STEPS)
  if (NOT DEFINED ${var})
    message(FATAL_ERROR "CheckStepIncludes.cmake needs -D${var}=...")
  endif()
endforeach()

get_filename_component(FILTERS_DIR "${FILTERS_DIR}" ABSOLUTE)
string(REPLACE "|" ";" steps "${STEPS}")

# The steps a step links directly, without other libraries.
foreach (step IN LISTS steps)
  string(REPLACE "|" ";" links "${LINKS_${step}}")
  set(direct_${step} "")
  foreach (lib IN LISTS links)
    if (lib IN_LIST steps)
      list(APPEND direct_${step} ${lib})
    endif()
  endforeach()
endforeach()

# The steps a step links directly or through other steps.
set(cycles "")
foreach (step IN LISTS steps)
  set(reachable "")
  set(queue ${direct_${step}})
  while (queue)
    list(POP_FRONT queue next)
    if (NOT next IN_LIST reachable)
      list(APPEND reachable ${next})
      list(APPEND queue ${direct_${next}})
    endif()
  endwhile()
  set(allowed_${step} ${reachable})
  if (step IN_LIST allowed_${step})
    list(APPEND cycles "${step}")
  endif()
endforeach()

if (cycles)
  list(JOIN cycles ", " cycle_text)
  message(FATAL_ERROR "Dependency cycle detected among steps: ${cycle_text}")
endif()

set(violations "")
foreach (step IN LISTS steps)
  file(GLOB_RECURSE files "${FILTERS_DIR}/${step}/*.h" "${FILTERS_DIR}/${step}/*.cpp")
  foreach (file IN LISTS files)
    file(RELATIVE_PATH shown "${FILTERS_DIR}" "${file}")
    file(STRINGS "${file}" include_lines REGEX "^[ \t]*#[ \t]*include")
    foreach (line IN LISTS include_lines)
      if (NOT line MATCHES "#[ \t]*include[ \t]*[\"<]([^\">]+)[\">]")
        continue()
      endif()
      set(path "${CMAKE_MATCH_1}")
      if (NOT shown MATCHES "/ui/" AND (path MATCHES "(^|/)ui/" OR path MATCHES "(^|/)core/gui/"))
        list(APPEND violations "  ${shown}: \"${path}\" (non-UI core file must not include UI headers)")
      endif()
      foreach (other IN LISTS steps)
        if (other STREQUAL step OR other IN_LIST allowed_${step})
          continue()
        endif()
        if (path MATCHES "(^|/)${other}/")
          list(APPEND violations "  ${shown}: \"${path}\" (${step} doesn't link ${other})")
        endif()
      endforeach()
    endforeach()
  endforeach()
endforeach()

if (violations)
  list(JOIN violations "\n" text)
  message(FATAL_ERROR "A step includes headers of a step it doesn't link:\n${text}\n"
                      "Link the step (only further down the chain) or move the code.")
endif()
list(LENGTH steps count)
message(STATUS "The includes of all ${count} steps follow their links.")
