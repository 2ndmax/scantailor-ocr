set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyDialog.ui
    ApplyMarginsDialog.ui
    OptionsWidget.ui)

# What processes the pages, without widgets.
set(sources
    Utils.cpp Utils.h
    OutputSizeOptions.cpp OutputSizeOptions.h
    Filter.cpp Filter.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Params.cpp Params.h
    Settings.cpp Settings.h
    Alignment.cpp Alignment.h
    OrderBySizeProvider.cpp OrderBySizeProvider.h
    Guide.cpp Guide.h)

add_library(page_layout STATIC ${sources})
target_link_libraries(page_layout PUBLIC core output)
# What the main window shows of the step.
set(ui_sources
    ImageView.cpp ImageView.h
    OptionsWidget.cpp OptionsWidget.h
    FilterUi.cpp FilterUi.h
    Thumbnail.cpp Thumbnail.h
    ApplyDialog.cpp ApplyDialog.h
    ApplyMarginsDialog.cpp ApplyMarginsDialog.h)

list(TRANSFORM ui_sources PREPEND "ui/")
list(TRANSFORM ui_files PREPEND "ui/")

add_library(page_layout_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(page_layout_ui PUBLIC page_layout core_gui)

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})