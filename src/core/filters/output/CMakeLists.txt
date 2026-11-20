set(CMAKE_AUTOUIC ON)

set(ui_files
    ApplyColorsDialog.ui
    OptionsWidget.ui
    OptionsWidgetBinarizationOtsu.ui
    OptionsWidgetBinarizationLocal.ui
    PictureZonePropDialog.ui)

# What processes the pages, without widgets.
set(sources
    ImageViewTab.h
    LastImageViewTab.h
    Filter.cpp Filter.h
    DefaultOutputParams.cpp DefaultOutputParams.h
    OutputParams.cpp OutputParams.h
    OrderByColorModeProvider.cpp OrderByColorModeProvider.h
    Task.cpp Task.h
    TaskResult.cpp TaskResult.h
    ThumbnailData.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    GeometryCollector.h
    OutputStatusCollector.h
    OutputGenerator.cpp OutputGenerator.h
    OutputMargins.h
    Settings.cpp Settings.h
    Utils.cpp Utils.h
    Params.cpp Params.h
    BlackWhiteOptions.cpp BlackWhiteOptions.h
    BlackWhiteBinarization.cpp BlackWhiteBinarization.h
    ColorCommonOptions.cpp ColorCommonOptions.h
    RenderParams.cpp RenderParams.h
    ColorParams.cpp ColorParams.h
    PhotoAdjustments.cpp PhotoAdjustments.h
    OutputImageParams.cpp OutputImageParams.h
    OutputFileParams.cpp OutputFileParams.h
    OutputParams.cpp OutputParams.h
    PictureLayerProperty.cpp PictureLayerProperty.h
    ZoneCategoryProperty.cpp ZoneCategoryProperty.h
    PictureZonePropFactory.cpp PictureZonePropFactory.h
    PictureZoneComparator.cpp PictureZoneComparator.h
    FillColorProperty.cpp FillColorProperty.h
    FillZonePropFactory.cpp FillZonePropFactory.h
    FillZoneComparator.cpp FillZoneComparator.h
    DespeckleState.cpp DespeckleState.h
    DespeckleVisualization.cpp DespeckleVisualization.h
    DespeckleLevel.cpp DespeckleLevel.h
    DewarpingOptions.cpp DewarpingOptions.h
    DepthPerception.cpp DepthPerception.h
    SplittingOptions.cpp SplittingOptions.h
    PictureShapeOptions.cpp PictureShapeOptions.h
    OutputProcessingParams.cpp OutputProcessingParams.h
    OutputImage.h
    OutputImageWithForeground.h
    OutputImageWithOriginalBackground.h
    OutputImagePlain.cpp OutputImagePlain.h
    OutputImageWithForegroundMask.cpp OutputImageWithForegroundMask.h
    OutputImageWithOriginalBackgroundMask.cpp OutputImageWithOriginalBackgroundMask.h
    OutputImageBuilder.cpp OutputImageBuilder.h ForegroundType.h)

add_library(output STATIC ${sources})
target_link_libraries(output PUBLIC core PRIVATE dewarping)
# What the main window shows of the step.
set(ui_sources
    ApplyColorsDialog.cpp ApplyColorsDialog.h
    ImageView.cpp ImageView.h
    TabbedImageView.cpp TabbedImageView.h
    OptionsWidget.cpp OptionsWidget.h
    FilterUi.cpp FilterUi.h
    Thumbnail.cpp Thumbnail.h
    BinarizationMethods.cpp BinarizationMethods.h
    PictureZonePropDialog.cpp PictureZonePropDialog.h
    PictureZoneEditor.cpp PictureZoneEditor.h
    FillZoneEditor.cpp FillZoneEditor.h
    ColorPickupInteraction.cpp ColorPickupInteraction.h
    DespeckleView.cpp DespeckleView.h
    DewarpingView.cpp DewarpingView.h
    DewarpingMethodBox.cpp DewarpingMethodBox.h
    OptionsWidgetBinarizationOtsu.cpp OptionsWidgetBinarizationOtsu.h
    OptionsWidgetBinarizationLocal.cpp OptionsWidgetBinarizationLocal.h
    OptionsWidgetBinarization.h)

list(TRANSFORM ui_sources PREPEND "ui/")
list(TRANSFORM ui_files PREPEND "ui/")

add_library(output_ui STATIC ${ui_sources} ${ui_files})
target_link_libraries(output_ui PUBLIC output core_gui PRIVATE dewarping)
target_include_directories(output PRIVATE "$<TARGET_PROPERTY:TIFF::TIFF,INTERFACE_INCLUDE_DIRECTORIES>")
target_include_directories(output_ui PRIVATE "$<TARGET_PROPERTY:TIFF::TIFF,INTERFACE_INCLUDE_DIRECTORIES>")

translation_sources(scantailor ${sources} ${ui_sources} ${ui_files})