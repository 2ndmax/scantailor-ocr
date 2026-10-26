add_executable(oblique_compare main.cpp TestPage.cpp TestPage.h Report.cpp Report.h)
target_link_libraries(oblique_compare PRIVATE deskew core ${EXTRA_LIBS})
