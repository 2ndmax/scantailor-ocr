set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    Filter.cpp Filter.h
    DeskewParams.cpp DeskewParams.h
    UiData.cpp UiData.h
    Settings.cpp Settings.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Dependencies.cpp Dependencies.h
    ObliqueFinder.cpp ObliqueFinder.h
    Params.cpp Params.h
    SkewDetection.h
    Utils.cpp Utils.h)

add_library(deskew STATIC ${sources})
target_link_libraries(deskew PUBLIC core select_content)
# What the main window shows of the step.
set(ui_sources
    ImageView.cpp ImageView.h
    Thumbnail.cpp Thumbnail.h
    OptionsWidget.cpp OptionsWidget.h
    FilterUi.cpp FilterUi.h
    ApplyDialog.cpp ApplyDialog.h)

list(TRANSFORM ui_sources PREPEND "ui/")
list(TRANSFORM ui_files PREPEND "ui/")

add_library(deskew_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(deskew_ui PUBLIC deskew core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})