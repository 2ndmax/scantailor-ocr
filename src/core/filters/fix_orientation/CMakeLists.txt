set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    ImageTrim.cpp ImageTrim.h
    Filter.cpp Filter.h
    FixOrientationParams.cpp FixOrientationParams.h
    Settings.cpp Settings.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Utils.cpp Utils.h)

add_library(fix_orientation STATIC ${sources})
target_link_libraries(fix_orientation PUBLIC core_base page_split)
target_include_directories(fix_orientation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
# What the main window shows of the step.
set(ui_sources
    ImageView.cpp ImageView.h
    OptionsWidget.cpp OptionsWidget.h
    ApplyDialog.cpp ApplyDialog.h
    FilterUi.cpp FilterUi.h)

list(TRANSFORM ui_sources PREPEND "ui/")
list(TRANSFORM ui_files PREPEND "ui/")

add_library(fix_orientation_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(fix_orientation_ui PUBLIC fix_orientation core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})