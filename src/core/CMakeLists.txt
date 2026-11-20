set(common_sources
    BackgroundExecutor.cpp BackgroundExecutor.h
    BackgroundTask.cpp BackgroundTask.h
    CommandLineRun.cpp CommandLineRun.h
    ConnectionManager.cpp ConnectionManager.h
    ContentBoxPropagator.cpp ContentBoxPropagator.h
    DefaultParams.cpp DefaultParams.h
    DefaultParamsProfileManager.cpp DefaultParamsProfileManager.h
    DefaultParamsProvider.cpp DefaultParamsProvider.h
    ExportJob.cpp ExportJob.h
    ExportPage.cpp ExportPage.h
    GlyphLessFont.cpp GlyphLessFont.h
    LoadFileTask.cpp LoadFileTask.h
    NewScanTracker.cpp NewScanTracker.h
    OcrEngine.cpp OcrEngine.h
    OcrLanguages.cpp OcrLanguages.h
    OcrTextOutputs.cpp OcrTextOutputs.h
    PageOrientationPropagator.cpp PageOrientationPropagator.h
    PathNames.cpp PathNames.h
    PdfImageEncoder.cpp PdfImageEncoder.h
    PdfPageOrder.cpp PdfPageOrder.h
    PdfWriter.cpp PdfWriter.h
    ProcessingTaskQueue.cpp ProcessingTaskQueue.h
    ProjectFiles.cpp ProjectFiles.h
    ProjectReader.cpp ProjectReader.h
    ProjectSession.cpp ProjectSession.h
    ProjectWriter.cpp ProjectWriter.h
    RecentProjects.cpp RecentProjects.h
    ScanInsertion.cpp ScanInsertion.h
    SelectedPage.cpp SelectedPage.h
    SettingsSetup.cpp SettingsSetup.h
    SmartFilenameOrdering.cpp SmartFilenameOrdering.h
    StageSequence.cpp StageSequence.h
    TessdataDownloader.cpp TessdataDownloader.h
    Translations.cpp Translations.h
    WorkerThreadPool.cpp WorkerThreadPool.h
    ImagePresentation.h
    NullTaskStatus.h
    PayloadEvent.h
    jbig2enc/jbig2arith.cpp jbig2enc/jbig2arith.h)

# Everything that works without widgets: processing, project files, export, the command line run.
add_subdirectory(zones)
add_subdirectory(base)
add_library(core STATIC ${common_sources})

# Third-party code, kept as in jbig2enc: no warnings for it.
if(MSVC)
  set_source_files_properties(jbig2enc/jbig2arith.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(jbig2enc/jbig2arith.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_link_libraries(
  core
  PRIVATE TIFF::TIFF PNG::PNG ZLIB::ZLIB JPEG::JPEG ${OPENJPEG_TARGET}
  PUBLIC Qt::Core Qt::Gui Qt::Xml Qt::Network Qt::OpenGL Qt::Svg
  core_base
  imageproc zones
  fix_orientation page_split deskew select_content page_layout output)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
translation_sources(scantailor ${common_sources})

target_sources(core PRIVATE OcrEngine.cpp)
target_link_libraries(core PRIVATE ${TESSERACT_TARGET})
translation_sources(scantailor OcrEngine.cpp)

add_subdirectory(gui)
if (BUILD_TESTS)
  add_subdirectory(tests)
endif()
add_subdirectory(filters/fix_orientation)
add_subdirectory(filters/page_split)
add_subdirectory(filters/deskew)
add_subdirectory(filters/select_content)
add_subdirectory(filters/page_layout)
add_subdirectory(filters/output)