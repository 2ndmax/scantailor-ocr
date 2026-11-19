# The steps as the main window shows them: one FilterUi per step and the thumbnails of each step. It sits
# above the steps' _ui libraries, which build on core_gui, so that no library needs one linked after it.
set(sources
    FilterUiSequence.cpp FilterUiSequence.h
    ThumbnailFactory.cpp ThumbnailFactory.h)

add_library(stages_ui STATIC ${sources})
target_link_libraries(
  stages_ui
  PUBLIC core_gui fix_orientation_ui page_split_ui deskew_ui select_content_ui page_layout_ui output_ui)
target_include_directories(stages_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

translation_sources(scantailor ${sources})
