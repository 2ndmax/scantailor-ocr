# Changes compared to ScanTailor Advanced 1.2.1

This list also serves as the notice of modifications required by the GPL. It is ordered by
topic, not by version.

## Name, version and project files

* The program is called **ScanTailor OCR**, starting with version **1.0.0**. The program file
  and the Linux package are `scantailor-ocr`, so it can be installed next to ScanTailor Advanced.
  The window title shows the version.
* New application logo: an open book inside scan corners on a dark blue tile, readable down to
  16 px (the Windows icon uses heavier strokes for the small sizes).
* On the first start, the settings, saved default profiles and OCR languages of ScanTailor
  Advanced are copied over once. The old files are kept.
* **Project files are saved as version 10** (`.ScanTailor`). Version 4, as written by ScanTailor
  Advanced 1.2 and by older versions of ScanTailor OCR, is still read. ScanTailor Advanced
  therefore no longer opens projects saved by ScanTailor OCR (it would have opened them without
  a warning and dropped the additional settings when saving).
* Project files and profiles are written safely: the old file is replaced only when everything
  has been written, so a full disk or a crash no longer leaves a broken project.
* **Settings for the whole project are stored in the project:** TIFF and PDF compression, text
  recognition (languages, text layout), color mode detection and white on black detection. A
  project can be reproduced on another computer, and each book can have its own settings. New
  projects take them from the default parameters, older projects (also those of ScanTailor
  Advanced) when they are first opened. Only "Open the PDF after creating it" stays a program
  setting. Up to version 1.0.10 these were program settings; their values are not taken over,
  so they start with the standard values and have to be set once in the default parameters.
* **Each project keeps its start values,** a copy of the default parameters that pages added
  later start with. Changing the profile no longer changes an open project; after OK in the
  Default Parameters, a question offers to use the new values for the open project, too.
* The project file lists the settings for the whole project and the start values first, then
  the images and pages, and the settings of each page last.
* "Show Hard Margins Rectangle" (context menu in *Margins*) is a program setting instead of a
  project setting, like the guides in *Output*.
* The chain buttons of the margins in *Margins* are no longer remembered: a page starts linked
  where its two margins are equal, as in the Default Parameters. A click applies while the page
  is shown.
* Only English and German are available. The Spanish, French, Korean, Polish, Portuguese and
  Russian translations were removed, as 60-73 % of their texts were untranslated; they can be
  restored from the commit history. Qt's own texts (standard buttons, file dialogs) are
  translated as well. Translations work again (the renamed program had stopped finding them),
  and the system language is picked on the first start also when only the language matches
  (`de` for `de_DE`).
* Settings: "Processor threads" in the group "Performance" is one limit for all parallel work
  (batch processing, PDF export, image filters, JPEG 2000); default: all threads but two.
  "Accent color" replaces the blue of hovered and focused controls, selected thumbnails and the
  progress bar (after a restart; not used by the native color scheme). "Deskew handle distance"
  sets how close the drag handles for rotation and oblique correction sit to the image center.

## PDF export

* **New step "7 Create PDF"** after "Output". It combines the existing output files of the
  project into one PDF and doesn't process pages itself; pages not yet output are grayed out.
  The page list, file and progress replace the page view, the options are on the left, the
  thumbnails are hidden, and there is no batch button.
* Pages are shown as tiles in project order with thumbnail and file name. Each can
  be ticked or unticked ("All" / "None"); ticked pages show their position in the PDF. The order
  is changed by drag and drop or with "Move forward" / "Move back". Ticks and order are kept
  while the project is open but not saved; new pages appear ticked after their predecessor.
* While a PDF is created, the other steps and opening, creating or closing a project are
  locked. The PDF is written to a temporary file first, so a failed or cancelled export never
  leaves a broken file or damages an existing one. Replacing an existing PDF is always
  confirmed. Several pages are prepared in parallel, with progress display and cancelling.
* **Small files by default:**
  * Black and white pages: lossless **JBIG2**.
  * Mixed pages with split output: layers; only the picture area of the background is stored
    (JPEG at full, half or one third resolution), the text is a lossless JBIG2 mask on top.
  * Posterized color pages: lossless Deflate palette images.
  * All other pages: JPEG. Mixed pages without split output make the PDF much larger; they get a
    warning with a lossy method (the text gets blurred) and a note with a lossless one.
* The panel **PDF Compression** has a part for each kind of page: *Color and Grayscale* (JPEG,
  **JPEG 2000**, Deflate or None; quality; resolution of the "Background layer" of split pages),
  *Posterized Pages* (Deflate, JPEG, JPEG 2000 or None) and *Black and White* (JBIG2, CCITT G4,
  Deflate or None). The options are stored in the project, saved when changed.
* **Quality warnings:** the panel **Quality** lists the compression of the ticked pages, in the
  output files (read from the TIFF files, so old files of other projects count correctly) and in
  the PDF, and marks lossy methods (JPEG; JPEG 2000 below quality 100). It warns about lossy
  TIFF files, about pages compressed with loss twice and about posterized pages that JPEG or
  JPEG 2000 converts to full color. Pages with such a problem, or with a problem of their files,
  show a warning sign above the thumbnail, pages with only a note an information sign. The tooltip of a tile names the color mode, the
  compression in the output folder and in the PDF, and the warnings; the file name shows its
  full text. Pages not yet output say so in their placeholder.
* **JPEG 2000** is encoded with [OpenJPEG](https://www.openjpeg.org/). The quality value is
  meant to look like the same JPEG value, 100 is lossless. It takes longer than JPEG, and some
  simple PDF programs can't show it.
* **JBIG2** makes black and white pages about a third smaller than CCITT G4 with exactly the
  same pixels. Only the lossless generic region coding is used, never the symbol mode that can
  swap similar characters. The arithmetic coder comes from
  [jbig2enc](https://github.com/agl/jbig2enc) (Apache License 2.0, see `src/core/jbig2enc`).
* **Text recognition (OCR)** with [Tesseract](https://github.com/tesseract-ocr/tesseract) adds an
  invisible text layer for searching, selecting and copying; the page images stay unchanged.
  * Several languages can be ticked at once. The text layout can be detected automatically or
    set to a single column or block. Pages with split output are recognized on their
    foreground. Pages are recognized in parallel.
  * Language files (`*.traineddata`, best from
    [tessdata_best](https://github.com/tesseract-ocr/tessdata_best)) are found next to the
    program, in the user's application data folder, in `TESSDATA_PREFIX` and, on Linux, in the
    system packages. If ticked languages are in different folders, the missing files are copied
    into the user's folder once.
  * **More languages can be downloaded in the program** ("More languages ..."), with search and
    file sizes, several at once. Files are checked against a checksum and only appear once
    complete. The system's proxy settings are used.
  * **OCR languages of the project that are missing on the computer** are listed ticked with a
    warning sign; unticking one removes it from the project. "More languages ..." ticks them
    for download, with the installed languages grayed out, and creating the PDF asks whether to
    download them first. If they are still missing after that, it asks whether to go on without
    them.
  * The invisible text uses Tesseract's "GlyphLessFont" (Apache License 2.0), embedded once.

## New project and automatic import

* The "Project Files" dialog has a third line, **Project File**, suggesting
  `<input folder>\<folder name>.ScanTailor`. An existing file is only overwritten after asking.
  After moving files to the other list, the next file is selected.
* With **Import new scans automatically from input folder** ticked, a project may start without
  images. It opens with the processing steps instead of the start page.
* Step 1 is called **Import**. Its panel **Automatic Import** watches a folder (e.g. the one a
  scanning program saves to) and adds every new image to the end of the project, once the file
  is completely written. The project is saved after each scan. Importing is off when a project
  is opened; images already in the folder can be chosen once when it is switched on.
* **Insert after selected page:** each new scan is inserted after the previous one and renamed
  `<anchor name>_<scan name>.tif`. **Replace selected page with next scan:** the next scan takes
  the place of the selected page(s), its settings and output files are dropped, and the old
  image is moved to a folder `replaced`.
* Scans arriving during batch processing or PDF creation are added afterwards. Scans without DPI
  are added and counted; their DPI can be set with *Tools > Fix DPI of All Images*.

## Image import

* **TIFF reading reworked:** tiled TIFF files, floating point, signed integer and 32-bit images
  are supported; palette colors of big-endian TIFF files are correct; read errors no longer
  yield uninitialized rows; unsupported compressions are rejected at import, naming the
  compression. Alpha handling in TIFFs is fixed.
* **JPEG 2000 import** (`.jp2 .j2k .j2c .jpc .jpf .jpx .jph .jhc`) through OpenJPEG: fast import,
  thumbnails at reduced resolution, multi-threaded decoding, huge images decoded strip by strip.
* Faster thumbnails and metadata reading for JPEG files.
* Fixed a leak in the JPEG metadata reader and a buffer bug in the PNG reader.
* Large color JPEG or PNG scans (more than 256 MB decoded) load again; Qt 6 refused them.

## Split Pages and page order

* The page type is switched with **Auto / Manual** buttons, as the split line is. The dialog
  behind "Change ..." is now an **Apply to ...** dialog with *Page type*, *Split line position*
  and *Detection method*, all checked by default. The four page layout buttons are redrawn
  (42 x 42 pixels, no text, tooltip names the layout).
* **Page order** (left half first or right half first) is set for the whole project in the new
  group "Page Order" of the *Split Pages* step, with icon buttons. It replaces the menu item
  "Reverse Two-Page Spread Order" and the check box "Right to left layout" of the new project.
  Switching renames the existing output files and thumbnails, so *Output* doesn't generate
  them again. The Default Parameters set the order of new projects (default: left first);
  existing projects keep their own.
* Switching from an image step to a page step selects only the first half of a single selected
  scan.

## Deskew and Import options

* *Import*: "Rotate" and "Crop scan (trim)" are one panel, **Rotate and Trim**. The trim values
  are named *Top*, *Bottom*, *Left* and *Right* and refer to the rotated view. A rotation of 0 is
  kept when the project is reopened.
* *Deskew*: the oblique (shear) correction has **Auto / Manual buttons** like the rotation.
  Switching oblique to *Auto* measures the page; the oblique finder reports no angle when the
  image has too little structure. Dragging a handle makes only its own angle manual. "Apply
  oblique automatically" is a master switch.
* **Fixed:** the oblique correction sheared by only half of the measured angle; it now shears by
  the whole angle (manual angles saved by older versions now shear twice as much). The deskew
  "Auto" button no longer loses its highlight.
* **Detection method per page** (*Content* / *Top page edge*) below Auto / Manual, replacing the
  project-wide check box. It is transferred with **Apply to ...** and set for new pages in the
  default parameters. In Auto mode, **Apply to ...** measures every target page on its own.
  The project stores the method per page; the project-wide setting is still written.

## Select Content

* **"Ignore page box during detection"**: the content is detected independently of the page
  box (the content box is still clipped to it), so text touching the page box is not removed
  with the border. Adopted from ScanTailor Advanced pull request #182. Each page keeps this
  setting, like "Fill factor" and "Border tolerance" (see below).
* "Page Box" and "Content Box" are one panel, **Page and Content Box**. The mode "Off" replaces
  "Disable". The mode buttons act when checked, not when pressed.
* Fixed several content detection and box editing bugs. Settings for the detection and the
  BlurDiv-based detection are described under the adopted features below.

## Output

* **Split output with "Original background"** (paper of the text areas kept as scanned, under
  crisp black text; written to `out/original_background`) works now. Fixed: a crash with
  dewarping off, darkened pictures with black blobs, and the paper tone being lost by the
  fill zone step.
* **New option "Grayscale output"** (output options and default parameters) for *Color /
  Grayscale* and *Mixed* mode: much smaller files. ([upstream #179](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/179))
* **Fixed a crash on mixed pages without content box**; such pages come out white.
  ([upstream #172](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/172))
* **TIFF compression** moved from the settings to the panel **TIFF Compression** with a part for
  each kind of page: *Color and Grayscale* (None, LZW, Deflate, LZMA, JPEG and its quality),
  *Posterized Pages* (None, LZW, Deflate, LZMA) and *Black and White* (None, LZW, Deflate, LZMA,
  CCITT G4). The options apply to the whole project. After a change, its output is created
  again. LZMA gives the smallest lossless files but is slow. Smaller files without a
  setting: horizontal predictor for LZW, Deflate and LZMA, 4 bits per pixel for posterized
  pages with up to 16 colors, YCbCr for color JPEG. The *Output* step shows the stored file, so
  JPEG losses are visible at once. Fixed: posterized pages couldn't be written with JPEG.
* **New filter "Remove color noise"** under *Output* -> "Enhancement" (first filter, also in the
  default parameters): turns weak colors, such as single red, green or blue pixels on gray paper,
  neutral gray (*Radius*, *Gray limit*) and keeps strong colors and the brightness.
* **Faster enhancement filters** (all processor cores, same results). With dewarping, they now
  work on the dewarped page in output resolution, as without dewarping.
* **New option "Even out letter widths"** under "Dewarping" (off by default, also in the default
  parameters): corrects letters that come out too narrow near the spine and too wide further
  away, by measuring stem widths of the dewarped page. Pages with too little text or deviations
  below 3 % are left alone. The correction is stored with the distortion model; dewarping
  takes about twice as long.
* **Dewarping:** automatic models that make the text lines horizontal are preferred (paper
  edges not parallel to the text no longer win); an endless loop for short text lines, a
  RANSAC bias of the content bounds and zones off by up to a pixel on dewarped pages are fixed.
  Changing the output resolution no longer turns automatic dewarping off, and the filling color
  "Black" is no longer ignored with dewarping.
* **Output cleanup:** removing pages, replacing a scan or switching a page between one and two
  pages deletes the output files in all folders (`foreground`, `background`,
  `original_background`, caches) together with their thumbnails.
* Fixed options overwritten by the display, a stale dewarping mode and stale thumbnails.
* **Wiener denoiser** has a check box and its own heading "Enhancement". The sliders *Depth
  perception* and *Despeckle* show their value above them; *Depth perception* is part of the
  **Dewarping** panel.
* The output resolution and the dewarping mode (with "Post deskew") apply to the current page
  at once; "Change ..." became **Apply to ...**.

## Options panels, dialogs and windows

* **One layout for all steps:** each panel holds what its **Apply to ...** button applies;
  parts are divided by bold headings, **Apply to ...** is the last line, and check boxes,
  fields and dependent settings are aligned the same way everywhere, also in the default
  parameters. Settings that don't apply are grayed out instead of hidden. Panels are at least
  320 pixels wide. All "apply to other pages" dialogs offer the same seven page choices.
* Merged or renamed panels: *Rotate and Trim*, *Deskew and Oblique*, *Page and Content Box*,
  *Source Resolution* (new, in *Margins*), *Output Size* (in *Margins*), *Output Resolution*,
  *Content* (formerly "Processing"; "Dark content on a light background"). In *Output*, the
  headings are "General", "Enhancement", "Filling", "Threshold", "Color Operations" and "Picture
  Zones"; "Equalize illumination (B&W)" and "(Color)" are one check box. In *Page Layout*,
  "Match size to all pages" is gone (it equals **Apply to ...** with all pages).
* Resolution fields accept 300, 400, 600 and 1200 dpi from a list or any typed value.
  Replacing an image file with one of a different pixel size takes over its resolution when
  plausible.
* Many new tooltips; panel titles, headings and menu entries in title case, "..." always after
  a space, "Folder" instead of "directory". All UI texts and the German translation were
  reviewed for consistent names. One space after each sentence.
* **Default Parameters dialog:** each tab is laid out like the options panel of its step. New in
  the dialog: Wiener denoiser, "Fill outside page box", the threshold methods Fox and Window,
  and Delta for Sauvola and Wolf. The window opens large enough to show every tab without
  scrolling, except *Output*. Fixed: wrong settings shown for Bradley, Grad, EdgePlus, BlurDiv
  and EdgeDiv; the Wolf upper bound not saved; the deskew angle limited to 0 to 99.99; profile
  names are checked before saving. The tab *Create PDF* holds the PDF compression and the text
  recognition, *Output* the TIFF compression, *Deskew* the white on black detection (moved from
  the settings). The profile "Custom" is kept like a saved profile; OK with unsaved changes to a
  profile asks whether to save them in the profile.
* **Fix DPI of All Images** and **relinking** (opened when images of the project are missing)
  use folder trees:
  * *Fix DPI*: one tree of folders and images with their resolution and traffic light icons
    (red: undefined, yellow: implausible, green: fine), a filter "Show only pages that need
    fixing", multiple selection with Ctrl and Shift, and "Apply to selection".
  * *Relinking*: an editable **Output Folder** path with "Browse ..." and a tree of the image
    folders with "n of m missing", "Show missing only", an editable path field and "Locate ...".
    Changes apply at once; a relinking that would merge several files into one is rejected.
    Earlier relinkings are kept; case-only path differences and drive roots are handled.
  * If the output folder is missing, it is created again when the images are all in place.
* **Zone editors:** the selection mode for new zones (polygon, lasso, rectangle) is a button in
  the status bar; keys Z, X and C still work, and the mode is kept across restarts. The hints
  explain each mode. ([upstream #15](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/15))
* **Thumbnails and page selection:** selected thumbnails have a blue border, the current page a
  stronger fill. The context menu has a submenu "Select" to select pages by pattern (all, this
  and following, odd, even, every other). "Every other selected page" works the same in all
  steps, and "Apply to ..." windows open with "Selected pages" when several pages are selected.
* **Start page:** buttons instead of links, recent projects listed with their folder below
  and the full path as tooltip.
* Status bar: file names up to 50 characters, and the image size is followed by its
  resolution, so a wrong source DPI is visible in every step. Number fields are sized from a
  sample value. Pixmaps (batch animation, thumbnails, PDF page icons) are sharp on scaled
  screens. The native color scheme on Windows 11 places number field arrows above each other.

## Error reporting and fixes

* When images can't be loaded or output files can't be written, the reason is shown in one
  collected, non-modal message. An unexpected error while processing a page shows the error on
  the page instead of ending the program, and batch processing continues. Output TIFF files are
  checked for complete writing. Malformed values in project files no longer black out a page.
* **Loss of changes on close fixed:** a change that kept the file size (e.g. a skew angle of 0.50
  to 0.75) closed the project without asking, and "Save" closed the project even when saving
  failed or was cancelled. Fixed page insertion next to split scans, the order of several
  inserted files, a crash in "Go to Page" without pages, and thumbnails left stale after a
  cancelled task.
* Natural file name sorting compares numbers digit by digit (long numbers such as time stamps)
  and the separators. **This can change page order in projects with inconsistent file naming.**
* Fixed: thumbnails stale with paths over 260 characters on Windows (also when saving the
  project), a changed source DPI having no effect until another step was chosen, *Fix DPI of
  All Images* losing changes and giving pages of a multi-page TIFF the wrong resolution, drag
  handles not reacting after Alt+Tab, round radio buttons at all font sizes, thumbnail list
  position after re-sorting, auto-save after page list changes.
* Fixed defects with wrong results or crashes found by static analysis and sanitizers: a
  grayscale measurement reading the wrong image, divisions by zero (Wolf on blank pages, content
  finder, missing DPI), integer overflows on large images, a use-after-free finishing a lasso
  zone, thread-safety issues with settings and profiles, wrong rounding in Savitzky-Golay
  smoothing, light pixels in the Robust method, an endless loop in text line refinement, and
  fixes in splines, the Hough transform and the image scaling and palette functions.

## Adopted from ScanTailor Experimental

Ideas and fixes taken over from
[ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental),
a separate fork of the original ScanTailor. Its code is structured differently, so most were
reimplemented rather than copied.

* **Scaling pages to the output size** in *Margins* (there: "match size mode"). The panel
  **Output Size**: *This Page* has "Match size" with **Off**, **Fill with margins** (as before)
  or **Scale** (content enlarged or reduced, margins keep their size in millimeters) and its own
  **Apply to ...**; *All Pages* shows the common size, an "Own target size" kept in the project
  and the limits "Enlarge to at most" and "Reduce to at most". Only the content is scaled,
  before binarization. Projects not using it give the same output as before. There is no
  separate scaling of width and height.
* **New filter "Even out background"** (there: "Subtract background") under *Output* ->
  "Enhancement": evens out stains, uneven yellowing and shadows near the spine (*Strength*,
  *Radius*). New: **"Make paper white"** divides each color channel by its own background,
  which removes a color cast.
* **New filters "KNN denoiser" and "Sharpen"** under "Enhancement", each with *Strength* and
  *Radius*. The filters run in the panel order: even out background, KNN denoiser, Wiener
  denoiser, sharpen. In color they work on the brightness.
* **Five new binarization methods** (under *Output* -> "Black and white"): **Gatos**, **Nick**,
  **Singh**, **Robust** and **MultiScale**; their code is taken over unchanged. Every method has
  a tooltip saying what it suits. The other methods were compared on sample pages and left out,
  as they hardly differed.
* **Settings for all methods:** **"Colored"** darkens colored pixels before the conversion so
  colored text comes out black; new: negative values lighten them, so colored stamps or
  highlighter marks disappear. **"Bounds"** (pixels darker or lighter always black or white)
  now apply to every method. Otsu shows a "Delta" field instead of its slider.
* **K-means for colored text** under "Color Operations": the list "Reduce colors" (None,
  Posterize, **K-means**) replaces the check box "Posterize". K-means finds the best "Colors"
  (2 to 16) in CIE Lab space, optionally with "Fixed black"; each letter gets one color per
  segment of the color segmentation, larger areas such as pictures keep their shading. Own,
  shorter and faster code.
* **Double-click shows the whole page again** in the image views, except *Select Content* and
  *Margins*.
* **The automatic content detection** in *Select Content* binarizes with BlurDiv instead of
  Wolf, so shadows at the spine and yellowed edges are less often taken for content.
* **Deliberately not adopted:**
  * "unPaper" and "Balance": "Make paper white" and the denoisers do this better.
  * The "horizontal barreling" correction of the dewarping: on synthetic and real test pages it
    brought no measurable improvement.

## Adopted from ScanTailor Spectre

Ideas and fixes taken over from
[ScanTailor Spectre](https://github.com/abandoned-industries/scantailor-spectre), a macOS fork of
ScanTailor Advanced.

* **Settings for the automatic content box detection** in *Select Content* (for art books and
  photo pages, also in the default parameters): "Fill factor" and "Border tolerance". Each page
  keeps its own; pages start with those of the default parameters, and **Apply to ...** with the
  content box takes them to other pages. Pages of older projects are not detected again because
  of this; they take the start values at their next detection. The defaults give the detection
  as before. The
  additional detection of photo plates relies on a macOS-only image analysis library and was
  not adopted.
* **Color mode detection** in *Output*: the color mode is chosen with three picture buttons
  instead of a list. The new group "Color Mode Detection" detects the color mode of chosen
  pages without creating output files. It was extended with "Color from" (a page counts as
  color only if enough of it is colored outside pictures), a rule that color only in pictures
  makes the page Mixed, and "Also look for grayscale pictures" with "Pictures from". The
  thumbnails of *Output* can be ordered by color mode.
* **Photo adjustments** in *Output*: the group "Photo Adjustments" after "Dewarping" has
  sliders for *Temperature*, *Tint*, *Exposure*, *Contrast*, *Highlights*, *Shadows*, *Whites*
  and *Blacks*. Unlike ScanTailor Spectre, they are applied to the finished output, so black
  and white text, picture detection and OCR stay as they were: Color / Grayscale changes the
  whole page, Mixed only the picture zones, Black and White is not affected. The values are
  saved per page and have their own "Apply to ...". Positive *Highlights* make bright parts
  brighter (ScanTailor Spectre does the opposite). The automatic suggestion was not adopted.
* **Split line along the spine** in *Split Pages* (also in the default parameters): the group
  "Split Line" has a "Method". "Edges" takes the vertical line closest to the center, as before,
  which can be the edge of a photo or a text column. "Spine line" looks for the spine as a thin
  line that is darker than both sides of it over the whole height of the page, also slightly
  tilted; if it finds none, "Edges" is used. "Spine line" is the default for new pages, and
  pages of older projects keep "Edges". The method is saved per page and is carried over by
  "Apply to ...". The approach follows the spine darkness search of ScanTailor Spectre, but
  the line has to show over the whole page height; the detection based on a macOS-only image
  analysis library was not adopted.
* **Seed fill propagates completely** (picture detection, background estimation, despeckling),
  and a wrong row stride in the 8-connected fill is fixed.
* **Faster grayscale conversion and histograms** of 32-bit color images with identical output.

## Build and tests

* Qt 5 is no longer supported; the program needs Qt 6.4 or later. Windows is built with
  MSVC and vcpkg using the Ninja generator (instructions in [BUILDING.md](BUILDING.md), the
  README holds the overview). CMake checks which compression schemes libtiff supports.
* Linux packages: .deb and AppImage, both with OCR. The Flatpak, the MXE cross-build script,
  RPM packaging and upstream's second workflow were removed. A script creates the portable
  Windows ZIP (`scripts\package-windows.ps1`).
* The release workflow builds the Windows ZIP and the Linux packages and attaches them; the
  shipped OCR languages (German, English) are checked against SHA-256. Started by hand, it
  builds as a test without a release. CI runs all tests (offscreen on Linux) on Linux and
  Windows, installs Tesseract, checks formatting with clang-format 22.1.3 and has a CodeQL
  workflow, started by hand.
* Static analysis: `-DENABLE_CODE_ANALYSIS=ON`, and scripts that run several compilers,
  clang-tidy (with a `.clang-tidy`), cppcheck and sanitizers and merge their findings.
* Optional developer tool `binarization_compare` (`-DBUILD_DEV_TOOLS=ON`): binarizes sample
  pages with our methods and those of ScanTailor Experimental and writes a web page that shows
  the results side by side. Its copy of ScanTailor Experimental code is in
  `src/tools/binarization_compare/experimental`.
* New tests for the image readers, the TIFF writer, the PDF export (structure, lossless G4 and
  JBIG2 round trips, text layer, language files, checksums), the Output step, K-means, page
  order and thumbnail files. The `update_translations` target works with Qt 6 again.
* **End-to-end test:** creates a project from scans drawn in the test, runs all six steps and
  the PDF export as the program does and checks the results (split, skew angle, content box,
  output files and their compression, color mode, saving and loading, PDF). For this, the chain
  of the steps, creating and opening a project and the page list of the PDF export moved from
  the main window into the core of the program, without a change in behavior; a command line
  version can use them later. A new project reads the profile only once.
