# The zones themselves: data saved in the project and used when processing pages.
set(data_sources
    SplineVertex.cpp SplineVertex.h
    SplineSegment.cpp SplineSegment.h
    EditableSpline.cpp EditableSpline.h
    SerializableSpline.cpp SerializableSpline.h
    Zone.cpp Zone.h
    ZoneSet.cpp ZoneSet.h
    EditableZoneSet.cpp EditableZoneSet.h)

# Drawing and editing zones in an image view.
set(editing_sources
    BasicSplineVisualizer.cpp BasicSplineVisualizer.h
    ZoneInteractionContext.cpp ZoneInteractionContext.h
    ZoneDefaultInteraction.cpp ZoneDefaultInteraction.h
    ZoneCreationInteraction.cpp ZoneCreationInteraction.h
    ZoneVertexDragInteraction.cpp ZoneVertexDragInteraction.h
    ZoneContextMenuInteraction.cpp ZoneContextMenuInteraction.h
    ZoneContextMenuItem.h
    ZoneDragInteraction.cpp ZoneDragInteraction.h
    ZoneCreationMode.h)

add_library(zones STATIC ${data_sources})
target_link_libraries(zones PUBLIC core)
target_include_directories(zones PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(zone_editing STATIC ${editing_sources})
target_link_libraries(zone_editing PUBLIC zones interaction)
target_include_directories(zone_editing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

translation_sources(scantailor ${data_sources} ${editing_sources})
