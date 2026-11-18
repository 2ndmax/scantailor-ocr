set(CMAKE_AUTOUIC ON)

set(ui_files
    SplitModeDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    SplitLineObject.h
    SplitDetectionMethod.cpp SplitDetectionMethod.h
    SpineLineFinder.cpp SpineLineFinder.h
    Params.cpp Params.h
    Dependencies.cpp Dependencies.h
    PageLayout.cpp PageLayout.h
    PageLayoutEstimator.cpp PageLayoutEstimator.h
    VertLineFinder.cpp VertLineFinder.h
    Filter.cpp Filter.h
    UiData.cpp UiData.h
    Settings.cpp Settings.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    LayoutType.cpp LayoutType.h
    OrderBySplitTypeProvider.cpp OrderBySplitTypeProvider.h
    PageLayoutAdapter.cpp PageLayoutAdapter.h
    Utils.cpp Utils.h)

add_library(page_split STATIC ${sources})
target_link_libraries(page_split PUBLIC core)
# What the main window shows of the step.
set(ui_sources
    UnremoveButton.cpp UnremoveButton.h
    ImageView.cpp ImageView.h
    Thumbnail.cpp Thumbnail.h
    OptionsWidget.cpp OptionsWidget.h
    SplitModeDialog.cpp SplitModeDialog.h
    FilterUi.cpp FilterUi.h)

add_library(page_split_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(page_split_ui PUBLIC page_split core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})