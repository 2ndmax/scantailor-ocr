set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    ImageTrim.cpp ImageTrim.h
    Filter.cpp Filter.h
    Settings.cpp Settings.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Utils.cpp Utils.h)

add_library(fix_orientation STATIC ${sources})
target_link_libraries(fix_orientation PUBLIC core)
# What the main window shows of the step.
set(ui_sources
    ImageView.cpp ImageView.h
    OptionsWidget.cpp OptionsWidget.h
    ApplyDialog.cpp ApplyDialog.h
    FilterUi.cpp FilterUi.h)

add_library(fix_orientation_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(fix_orientation_ui PUBLIC fix_orientation core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})