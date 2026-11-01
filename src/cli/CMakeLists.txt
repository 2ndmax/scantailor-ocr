# The command line tool. A console program, unlike the app.
add_executable(scantailor-ocr-cli main.cpp)
target_link_libraries(scantailor-ocr-cli PRIVATE core ${EXTRA_LIBS})
install(TARGETS scantailor-ocr-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

translation_sources(scantailor main.cpp)
