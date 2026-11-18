set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    Filter.cpp Filter.h
    UiData.cpp UiData.h
    ContentBoxFinder.cpp ContentBoxFinder.h
    PageFinder.cpp PageFinder.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Dependencies.cpp Dependencies.h
    DetectionOptions.cpp DetectionOptions.h
    Params.cpp Params.h
    Settings.cpp Settings.h
    PhysSizeCalc.cpp PhysSizeCalc.h
    OrderBySizeProvider.cpp OrderBySizeProvider.h
    Utils.cpp Utils.h)

add_library(select_content STATIC ${sources})
target_link_libraries(select_content PUBLIC core)
# What the main window shows of the step.
set(ui_sources
    ImageView.cpp ImageView.h
    OptionsWidget.cpp OptionsWidget.h
    ApplyDialog.cpp ApplyDialog.h
    FilterUi.cpp FilterUi.h
    Thumbnail.cpp Thumbnail.h)

add_library(select_content_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(select_content_ui PUBLIC select_content core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})
