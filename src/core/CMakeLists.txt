set(common_sources
    Translations.cpp Translations.h
    CommandLineRun.cpp CommandLineRun.h
    BackgroundExecutor.cpp BackgroundExecutor.h
    PixmapRenderer.cpp PixmapRenderer.h
    BubbleAnimation.cpp BubbleAnimation.h
    SmartFilenameOrdering.cpp SmartFilenameOrdering.h
    AbstractRelinker.h
    RelinkablePath.cpp RelinkablePath.h
    RelinkedKeys.h
    ImageInfo.cpp ImageInfo.h
    ImageFileInfo.cpp ImageFileInfo.h
    ImageMetadata.cpp ImageMetadata.h
    RecentProjects.cpp RecentProjects.h
    OutOfMemoryHandler.cpp OutOfMemoryHandler.h
    PageSelectionAccessor.cpp PageSelectionAccessor.h
    PageSelectionProvider.h
    ContentSpanFinder.cpp ContentSpanFinder.h
    ImageTransformation.cpp ImageTransformation.h
    ImagePixmapUnion.h
    DownscaledImage.cpp DownscaledImage.h
    ImagePresentation.h
    ThumbnailLoadResult.h
    ThumbnailPixmapCache.cpp ThumbnailPixmapCache.h
    OverlayStyle.cpp OverlayStyle.h
    ThumbnailDataBase.h
    ContentBoxPropagator.cpp ContentBoxPropagator.h
    PageOrientationPropagator.cpp PageOrientationPropagator.h
    DebugImagesImpl.cpp DebugImagesImpl.h
    ImageId.cpp ImageId.h
    PageId.cpp PageId.h
    PageInfo.cpp PageInfo.h
    BackgroundTask.cpp BackgroundTask.h
    ProcessingTaskQueue.cpp ProcessingTaskQueue.h
    PageSequence.cpp PageSequence.h
    StageSequence.cpp StageSequence.h
    ProjectPages.cpp ProjectPages.h
    FilterData.cpp FilterData.h
    ImageMetadataLoader.cpp ImageMetadataLoader.h
    ImageLoadErrors.cpp ImageLoadErrors.h
    TiffDevice.cpp TiffDevice.h
    TiffReader.cpp TiffReader.h
    Jp2Reader.cpp Jp2Reader.h
    Jp2MetadataLoader.cpp Jp2MetadataLoader.h
    TiffWriter.cpp TiffWriter.h
    ProcessingOptions.cpp ProcessingOptions.h
    PngMetadataLoader.cpp PngMetadataLoader.h
    TiffMetadataLoader.cpp TiffMetadataLoader.h
    JpegMetadataLoader.cpp JpegMetadataLoader.h
    ImageLoader.cpp ImageLoader.h
    ImageSizeLimit.cpp ImageSizeLimit.h
    OrthogonalRotation.cpp OrthogonalRotation.h
    WorkerThreadPool.cpp WorkerThreadPool.h
    LoadFileTask.cpp LoadFileTask.h
    FilterUiInterface.h
    ProjectReader.cpp ProjectReader.h
    ProjectWriter.cpp ProjectWriter.h
    ProjectFiles.cpp ProjectFiles.h
    PathNames.cpp PathNames.h
    ProjectSession.cpp ProjectSession.h
    SettingsSetup.cpp SettingsSetup.h
    EstimateBackground.cpp EstimateBackground.h
    Despeckle.cpp Despeckle.h
    FileNameDisambiguator.cpp FileNameDisambiguator.h
    OpenGLSupport.cpp OpenGLSupport.h
    OutputFileNameGenerator.cpp OutputFileNameGenerator.h
    Margins.cpp Margins.h
    ColorScheme.h
    PageRange.cpp PageRange.h
    SelectedPage.cpp SelectedPage.h
    Utils.cpp Utils.h
    PageView.h
    AutoManualMode.cpp AutoManualMode.h
    AbstractCommand.h
    AbstractFilter.h
    BeforeOrAfter.h
    FilterResult.h
    BatchProcessingResult.h
    CompositeCacheDrivenTask.h
    PageOrderProvider.h PageOrderProvider.cpp
    PageOrderOption.h
    PayloadEvent.h
    AbstractFilterDataCollector.h
    ThumbnailCollector.h
    ContentBoxCollector.h
    PageOrientationCollector.h
    ImageViewInfoProvider.cpp ImageViewInfoProvider.h
    ImageViewInfoListener.h
    UnitsProvider.cpp UnitsProvider.h
    UnitsListener.h UnitsListener.cpp
    UnitsConverter.cpp UnitsConverter.h
    Units.cpp Units.h
    DefaultParams.cpp DefaultParams.h
    DefaultParamsProfileManager.cpp DefaultParamsProfileManager.h
    DefaultParamsProvider.cpp DefaultParamsProvider.h
    DeviationProvider.h
    OrderByDeviationProvider.cpp OrderByDeviationProvider.h
    BlackOnWhiteEstimator.cpp BlackOnWhiteEstimator.h
    ImageSettings.cpp ImageSettings.h
    NullTaskStatus.h
    OrderByCompletenessProvider.cpp OrderByCompletenessProvider.h
    OrderBySizeProviderBase.cpp OrderBySizeProviderBase.h
    ColorSchemeFactory.cpp ColorSchemeFactory.h
    ApplicationSettings.cpp ApplicationSettings.h
    IconProvider.cpp IconProvider.h
    IconPack.h
    AbstractIconPack.cpp AbstractIconPack.h
    StyledIconPack.cpp StyledIconPack.h
    ConnectionManager.cpp ConnectionManager.h
    ZoneModeListener.h
    ContentMask.cpp ContentMask.h
    PdfWriter.cpp PdfWriter.h
    PdfCompression.h
    PdfImageEncoder.cpp PdfImageEncoder.h
    jbig2enc/jbig2arith.cpp jbig2enc/jbig2arith.h
    ExportPage.cpp ExportPage.h
    ExportJob.cpp ExportJob.h
    OcrTextOutputs.cpp OcrTextOutputs.h
    PdfPageOrder.cpp PdfPageOrder.h
    NewScanTracker.cpp NewScanTracker.h
    ScanInsertion.cpp ScanInsertion.h
    GlyphLessFont.cpp GlyphLessFont.h
    OcrLanguages.cpp OcrLanguages.h
    TessdataDownloader.cpp TessdataDownloader.h
    OcrEngine.h
    PictureDetection.cpp PictureDetection.h
    ColorModeDetector.h LeptonicaDetector.h WhiteBalance.h)

# Everything that works without widgets: processing, project files, export, the command line run.
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
  imageproc zones
  fix_orientation page_split deskew select_content page_layout output)
target_include_directories(core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
translation_sources(scantailor ${common_sources})

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
    ZoneEditorBase.cpp ZoneEditorBase.h)

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

target_sources(core PRIVATE OcrEngine.cpp ColorModeDetector.cpp LeptonicaDetector.cpp WhiteBalance.cpp)
target_link_libraries(core PRIVATE ${TESSERACT_TARGET})
translation_sources(scantailor OcrEngine.cpp)

add_subdirectory(interaction)
add_subdirectory(zones)
if (BUILD_TESTS)
  add_subdirectory(tests)
endif()
add_subdirectory(filters/fix_orientation)
add_subdirectory(filters/page_split)
add_subdirectory(filters/deskew)
add_subdirectory(filters/select_content)
add_subdirectory(filters/page_layout)
add_subdirectory(filters/output)
