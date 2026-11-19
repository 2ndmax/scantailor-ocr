# The zones themselves: data saved in the project and used when processing pages.
set(data_sources
    SplineVertex.cpp SplineVertex.h
    SplineSegment.cpp SplineSegment.h
    EditableSpline.cpp EditableSpline.h
    SerializableSpline.cpp SerializableSpline.h
    Zone.cpp Zone.h
    ZoneSet.cpp ZoneSet.h
    EditableZoneSet.cpp EditableZoneSet.h)

add_library(zones STATIC ${data_sources})
target_link_libraries(zones PUBLIC core)
target_include_directories(zones PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

translation_sources(scantailor ${data_sources})
