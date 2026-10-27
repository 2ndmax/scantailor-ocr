# The Light and Dark color schemes share one style sheet and one set of icons, in
# src/resources/color_schemes/template. Their colors are placeholders such as {{accent}};
# src/resources/color_schemes/<scheme>.colors gives each one its color, as "role = #rrggbb".
# {{scheme}} becomes the name of the scheme, which is also the prefix of its resources.
#
# generate_color_scheme(<scheme> <qrc variable>) writes the files of <scheme> and its .qrc into the
# build directory, at configure time, and returns the path of the .qrc. Changing a template or a
# color table runs the configuration again.
function(generate_color_scheme scheme qrc_var)
  set(source_dir "${SCANTAILOR_RESOURCES_DIR}/color_schemes")
  set(template_dir "${source_dir}/template")
  set(colors_file "${source_dir}/${scheme}.colors")
  set(output_dir "${CMAKE_BINARY_DIR}/color_schemes/${scheme}_scheme")

  file(STRINGS "${colors_file}" color_lines)
  set(roles "")
  foreach(line IN LISTS color_lines)
    if(line MATCHES "^[ \t]*$" OR line MATCHES "^[ \t]*#")
      continue()
    endif()
    if(NOT line MATCHES "^[ \t]*([A-Za-z0-9_]+)[ \t]*=[ \t]*([^ \t\r]+)[ \t\r]*$")
      message(FATAL_ERROR "${colors_file}: not \"role = color\": ${line}")
    endif()
    set(color_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    list(APPEND roles "${CMAKE_MATCH_1}")
  endforeach()

  file(GLOB_RECURSE templates RELATIVE "${template_dir}" CONFIGURE_DEPENDS "${template_dir}/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${colors_file}")
  foreach(template IN LISTS templates)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${template_dir}/${template}")
    file(READ "${template_dir}/${template}" content)
    string(REPLACE "{{scheme}}" "${scheme}" content "${content}")
    foreach(role IN LISTS roles)
      string(REPLACE "{{${role}}}" "${color_${role}}" content "${content}")
    endforeach()
    if(content MATCHES "{{([A-Za-z0-9_]+)}}")
      message(FATAL_ERROR "${colors_file} has no color for {{${CMAKE_MATCH_1}}} in ${template}")
    endif()

    if(template STREQUAL "scheme.qrc")
      set(output "${output_dir}/${scheme}_scheme.qrc")
    else()
      set(output "${output_dir}/${template}")
    endif()
    # configure_file() writes only when it changed, so that the resources are not compiled again on
    # every configuration.
    file(WRITE "${output}.tmp" "${content}")
    configure_file("${output}.tmp" "${output}" COPYONLY)
    file(REMOVE "${output}.tmp")
  endforeach()

  set(${qrc_var} "${output_dir}/${scheme}_scheme.qrc" PARENT_SCOPE)
endfunction()
