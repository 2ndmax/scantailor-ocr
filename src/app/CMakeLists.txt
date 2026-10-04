set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(target_name scantailor-ocr)

set(resource_files
    resources.qrc
    dark_scheme/dark_scheme.qrc
    light_scheme/light_scheme.qrc)
list_items_prepend(resource_files "${SCANTAILOR_RESOURCES_DIR}/")

set(gui_only_sources
    RelinkablePathVisualization.cpp RelinkablePathVisualization.h
    RelinkingModel.cpp RelinkingModel.h
    RelinkingSortingModel.cpp RelinkingSortingModel.h
    RelinkingListView.cpp RelinkingListView.h
    RelinkingDialog.cpp RelinkingDialog.h
    SettingsDialog.cpp SettingsDialog.h
    FixDpiDialog.cpp FixDpiDialog.h
    LoadFilesStatusDialog.cpp LoadFilesStatusDialog.h
    ImageLoadErrorNotifier.cpp ImageLoadErrorNotifier.h
    ProjectCreationContext.cpp ProjectCreationContext.h
    ProjectOpeningContext.cpp ProjectOpeningContext.h
    OutOfMemoryDialog.cpp OutOfMemoryDialog.h
    ThumbnailSequence.cpp ThumbnailSequence.h
    ProjectFilesDialog.cpp ProjectFilesDialog.h
    NewOpenProjectPanel.cpp NewOpenProjectPanel.h
    SystemLoadWidget.cpp SystemLoadWidget.h
    MainWindow.cpp MainWindow.h
    main.cpp
    StatusBarPanel.cpp StatusBarPanel.h
    DefaultParamsDialog.cpp DefaultParamsDialog.h
    PdfExportDialog.cpp PdfExportDialog.h
    TessdataDownloadDialog.cpp TessdataDownloadDialog.h)

set(gui_only_ui_files
    AboutDialog.ui
    BatchProcessingLowerPanel.ui
    DefaultParamsDialog.ui
    FixDpiDialog.ui
    LoadFilesStatusDialog.ui
    MainWindow.ui
    NewOpenProjectPanel.ui
    OutOfMemoryDialog.ui
    ProjectFilesDialog.ui
    RelinkingDialog.ui
    RemovePagesDialog.ui
    SettingsDialog.ui
    StatusBarPanel.ui
    SystemLoadWidget.ui)

set(win32_resource_file "")
if (WIN32)
  string(REPLACE "/" "\\\\" SCANTAILOR_WIN_DIR_NATIVE "${SCANTAILOR_RESOURCES_DIR}/win32")
  configure_file("${SCANTAILOR_RESOURCES_DIR}/win32/resources.rc.in" "${CMAKE_BINARY_DIR}/resources.rc" @ONLY)
  set(rc_file "${CMAKE_BINARY_DIR}/resources.rc")
  file(GLOB win32_resources "${SCANTAILOR_RESOURCES_DIR}/win32/*.ico")
  set_source_files_properties(
      "${rc_file}" PROPERTIES
      OBJECT_DEPENDS ${win32_resources})
  if (MINGW)
    # CMake doesn't know how to process .rc files with MinGW.
    set(win32_resource_file "${CMAKE_BINARY_DIR}/win32_resources.o")
    add_custom_command(
        OUTPUT "${win32_resource_file}"
        WORKING_DIRECTORY "${SCANTAILOR_RESOURCES_DIR}/win32"
        COMMAND windres -i "${rc_file}" -o "${win32_resource_file}"
        MAIN_DEPENDENCY "${rc_file}"
        DEPENDS ${win32_resources})
  else()
    set(win32_resource_file "${rc_file}")
  endif()
endif()

add_executable(
    ${target_name} WIN32 ${gui_only_sources} ${gui_only_ui_files}
    ${resource_files} ${win32_resource_file})
target_link_libraries(
    ${target_name}
    PRIVATE core ${EXTRA_LIBS})
target_include_directories(
    ${target_name}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<TARGET_PROPERTY:TIFF::TIFF,INTERFACE_INCLUDE_DIRECTORIES>")
install(TARGETS ${target_name} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if (Qt6_FOUND AND WIN32)
  #manage plugins with windeployqt, if the Qt installation ships it (vcpkg does not).
  find_program(
      WINDEPLOYQT_EXECUTABLE windeployqt
      HINTS "${QT_BINDIR}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
      "${QT6_INSTALL_PREFIX}/tools/Qt6/bin"
      DOC "Path to the windeployqt deployment tool.")

  if (WINDEPLOYQT_EXECUTABLE)
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}" $<TARGET_FILE:${target_name}>
      COMMENT "deploy qt files alongside binary (e.g. plugins)"
    )
    install(CODE "execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" \"\$\{CMAKE_INSTALL_PREFIX\}\")")
  else()
    #No windeployqt available: copy the plugins the application needs ourselves.
    if (QT6_INSTALL_PLUGINS)
      set(qt_plugins_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
    else()
      set(qt_plugins_dir "${QT6_INSTALL_PREFIX}/plugins")
    endif()
    message(STATUS "windeployqt not found, deploying Qt plugins from ${qt_plugins_dir}")

    # TLS (HTTPS, for downloading OCR languages): only Windows' own implementation (Schannel).
    # The OpenSSL backend needs OpenSSL DLLs, which aren't shipped.  Qt stops loading TLS
    # plugins at the first one that fails to load, so it would hide the Schannel backend.
    file(GLOB tls_plugins
         "${qt_plugins_dir}/tls/qschannelbackend*.dll"
         "${qt_plugins_dir}/tls/qcertonlybackend*.dll")
    if (tls_plugins)
      add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target_name}>/tls"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${tls_plugins} "$<TARGET_FILE_DIR:${target_name}>/tls"
        COMMENT "deploy qt tls plugins (Schannel) alongside binary"
      )
      install(FILES ${tls_plugins} DESTINATION "${CMAKE_INSTALL_BINDIR}/tls")
    endif()

    foreach (plugin_group platforms styles imageformats iconengines)
      if (EXISTS "${qt_plugins_dir}/${plugin_group}")
        add_custom_command(
          TARGET ${target_name}
          POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${qt_plugins_dir}/${plugin_group}"
              "$<TARGET_FILE_DIR:${target_name}>/${plugin_group}"
          COMMENT "deploy qt ${plugin_group} plugins alongside binary"
        )
        install(
            DIRECTORY "${qt_plugins_dir}/${plugin_group}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}")
      endif()
    endforeach()
  endif()
endif()

# Same set as the other sources ("scantailor"), which is the one update_translations works on.
translation_sources(scantailor ${gui_only_sources} ${gui_only_ui_files})
