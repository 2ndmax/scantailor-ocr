# The user interface on top of core: widgets, image views, the steps' panels.
set(gui_ui_files
    ErrorWidget.ui)

set(gui_sources
    Application.cpp Application.h
    ProcessingIndicationWidget.cpp ProcessingIndicationWidget.h
    NonOwningWidget.cpp NonOwningWidget.h
    ImageViewBase.cpp ImageViewBase.h
    BasicImageView.cpp BasicImageView.h
    StageListView.cpp StageListView.h
    DebugImageView.cpp DebugImageView.h
    TabbedDebugImages.cpp TabbedDebugImages.h
    ThumbnailBase.cpp ThumbnailBase.h
    ThumbnailFactory.cpp ThumbnailFactory.h
    IncompleteThumbnail.cpp IncompleteThumbnail.h
    ErrorWidget.cpp ErrorWidget.h
    FilterOptionsWidget.cpp FilterOptionsWidget.h
    StyleSheetColorScheme.cpp StyleSheetColorScheme.h
    FocusFrameStyle.cpp FocusFrameStyle.h
    NativeColorScheme.cpp NativeColorScheme.h
    ColorSchemeManager.cpp ColorSchemeManager.h
    WidgetUtils.cpp WidgetUtils.h
    AbstractFilterUi.h
    FilterUiSequence.cpp FilterUiSequence.h
    ChangedStateItemDelegate.h
    SkinnedButton.cpp SkinnedButton.h
    CollapsibleGroupBox.cpp CollapsibleGroupBox.h
    SectionHeading.cpp SectionHeading.h
    TiffCompressionPanel.cpp TiffCompressionPanel.h
    PageScopeGroupBox.cpp PageScopeGroupBox.h
    ZoneEditorBase.cpp ZoneEditorBase.h
    PixmapRenderer.cpp PixmapRenderer.h
    BubbleAnimation.cpp BubbleAnimation.h
    ImagePixmapUnion.h
    OverlayStyle.cpp OverlayStyle.h
    OpenGLSupport.cpp OpenGLSupport.h
    ColorScheme.h
    ColorSchemeFactory.cpp ColorSchemeFactory.h
    ImageViewInfoProvider.cpp ImageViewInfoProvider.h
    ImageViewInfoListener.h
    IconProvider.cpp IconProvider.h
    IconPack.h
    AbstractIconPack.cpp AbstractIconPack.h
    StyledIconPack.cpp StyledIconPack.h
    ZoneModeListener.h
    PageSelectionAccessor.cpp PageSelectionAccessor.h
    PageSelectionProvider.h)

add_library(core_gui STATIC ${gui_sources} ${gui_ui_files})
target_link_libraries(
  core_gui
  PUBLIC core Qt::Widgets Qt::OpenGLWidgets interaction zone_editing
  fix_orientation_ui page_split_ui deskew_ui select_content_ui page_layout_ui output_ui)
# core_gui, interaction, zone_editing and the steps' _ui libraries need each other, and GNU ld searches each
# static library only once, in link order. CMake repeats such a ring of libraries twice by default; then an
# object file that the second round pulls in can need one from a library already passed (zone_editing needs
# DragWatcher from interaction). A third round resolves that. The MSVC linker searches the libraries again
# until nothing is left, so only the Linux build in CI shows a mistake here.
set_property(TARGET core_gui PROPERTY LINK_INTERFACE_MULTIPLICITY 3)
target_include_directories(core_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(core_gui PROPERTIES AUTOUIC ON)

translation_sources(scantailor ${gui_sources} ${gui_ui_files})
