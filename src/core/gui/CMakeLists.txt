# The user interface on top of core: widgets, image views, mouse interaction and zone editing.
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
    IncompleteThumbnail.cpp IncompleteThumbnail.h
    ErrorWidget.cpp ErrorWidget.h
    FilterOptionsWidget.cpp FilterOptionsWidget.h
    StyleSheetColorScheme.cpp StyleSheetColorScheme.h
    FocusFrameStyle.cpp FocusFrameStyle.h
    NativeColorScheme.cpp NativeColorScheme.h
    ColorSchemeManager.cpp ColorSchemeManager.h
    WidgetUtils.cpp WidgetUtils.h
    AbstractFilterUi.h
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

# Mouse interaction in the image views.
set(interaction_sources
    DraggableObject.h
    DraggablePoint.cpp DraggablePoint.h
    DraggableLineSegment.cpp DraggableLineSegment.h
    ObjectDragHandler.cpp ObjectDragHandler.h
    InteractionHandler.cpp InteractionHandler.h
    InteractionState.cpp InteractionState.h
    DragHandler.cpp DragHandler.h
    DragWatcher.cpp DragWatcher.h
    ZoomHandler.cpp ZoomHandler.h
    InteractiveXSpline.cpp InteractiveXSpline.h
    DraggablePolygon.cpp DraggablePolygon.h
    RectHandleGeometry.h)
list(TRANSFORM interaction_sources PREPEND "interaction/")

# Drawing and editing zones in an image view; the zones themselves are in core/zones.
set(zone_editing_sources
    BasicSplineVisualizer.cpp BasicSplineVisualizer.h
    ZoneInteractionContext.cpp ZoneInteractionContext.h
    ZoneDefaultInteraction.cpp ZoneDefaultInteraction.h
    ZoneCreationInteraction.cpp ZoneCreationInteraction.h
    ZoneVertexDragInteraction.cpp ZoneVertexDragInteraction.h
    ZoneContextMenuInteraction.cpp ZoneContextMenuInteraction.h
    ZoneContextMenuItem.h
    ZoneDragInteraction.cpp ZoneDragInteraction.h
    ZoneCreationMode.h)
list(TRANSFORM zone_editing_sources PREPEND "zone_editing/")

add_library(core_gui STATIC ${gui_sources} ${gui_ui_files} ${interaction_sources} ${zone_editing_sources})
target_link_libraries(core_gui PUBLIC core zones Qt::Widgets Qt::OpenGLWidgets)
target_include_directories(
  core_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/interaction"
  "${CMAKE_CURRENT_SOURCE_DIR}/zone_editing")
set_target_properties(core_gui PROPERTIES AUTOUIC ON)

translation_sources(scantailor ${gui_sources} ${gui_ui_files} ${interaction_sources} ${zone_editing_sources})

add_subdirectory(stages)
