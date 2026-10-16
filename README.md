# ScanTailor OCR – scanned pages to searchable PDF

ScanTailor OCR cleans up scanned pages and turns them into a **compact, searchable PDF**:

* **PDF export** built in, directly from the project's output: lossless JBIG2 for black and
  white pages, layered pages (text over a small JPEG picture) for mixed pages.
* **Text recognition (OCR)** with Tesseract adds an invisible text layer, so the PDF can be
  searched and its text copied. More languages can be downloaded in the program.

ScanTailor OCR is based on [ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced)
1.2.1, an interactive post-processing tool for scanned pages (page splitting, deskewing, content
selection, margins, dewarping and more).

**For the page processing features and how to use them, please see the
[documentation of ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced#readme).**
Everything described there applies to ScanTailor OCR as well.

Downloads for Windows and Linux are on the [releases page](https://github.com/2ndmax/scantailor-ocr/releases).

The changes were developed with the help of Claude (Anthropic).

## Changes compared to ScanTailor Advanced 1.2.1

### Name and version

* The program is now called **ScanTailor OCR**, starting with version **1.0.0**. The program
  file is `scantailor-ocr` (`scantailor-ocr.exe`), the Linux package `scantailor-ocr`, so it can
  be installed next to ScanTailor Advanced.
* On the first start, the settings, saved default profiles and OCR languages of ScanTailor
  Advanced (`scantailor-advanced`) are copied over once. The old files are kept.
* The project file format (`.ScanTailor`) is unchanged; projects remain compatible with
  ScanTailor Advanced.

### PDF export

* **New step "7 Create PDF"** in the list of steps, after "Output". It combines the existing
  output files of the project into one PDF. It doesn't process pages itself: pages that haven't
  been output yet are shown greyed out. There's no separate window: the page list, the PDF file
  and the progress take the place of the page view, the options are shown on the left like
  those of the other steps, and the thumbnails are hidden. The step has no batch processing
  button.
* The pages are shown as tiles in project order, from left to right and then on the next row,
  each with its thumbnail, file name and kind of page. Each page can be ticked or unticked
  ("All" / "None"); the ticked pages are numbered with their position in the PDF, the others are
  shown faded. The order can be changed with drag and drop or with "Move forward" /
  "Move back". Pages not output yet get a placeholder in the proportions of the other pages.
* The ticks and the order are kept while the project is open, also when going back to an
  earlier step and outputting pages again. Each time the step is shown, the pages are read
  again: new pages appear ticked after the page that precedes them in the project, removed
  pages disappear. The selection and order aren't saved, and the project file isn't changed.
* While a PDF is being created, the other steps and opening, creating or closing a project are
  locked. Closing the program asks whether to cancel the PDF.
* Small files, similar in size to Adobe Acrobat's output, by default:
  * Black and white pages are stored losslessly as **JBIG2**.
  * Mixed pages with **split output** are stored in layers. Only the picture area of the
    background is stored, as JPEG at reduced resolution (full, half or one third). The text is
    painted losslessly on top as a JBIG2 mask.
  * Posterized color pages keep their few colors: they are stored losslessly with **Deflate** as
    palette images, which is sharp and small.
  * All other pages are stored as JPEG. Mixed pages without split output get a warning, as they
    make the PDF much larger.
* The panel **PDF Compression** has a part for each kind of page:
  * *Color and Grayscale* (also the pictures of split pages): JPEG (default), **JPEG 2000**,
    Deflate or None, the quality and the resolution of the pictures of split pages
    ("Background layer").
  * *Posterized Pages* (also posterized pictures of split pages): Deflate (default), JPEG,
    JPEG 2000 or None, and the quality. Posterized grayscale pages can't be told apart from
    other grayscale pages and follow *Color and Grayscale*.
  * *Black and White* (also the text of split pages): JBIG2 (default), CCITT G4, Deflate or None.

  Deflate and None are lossless; Deflate uses PNG predictors for photos. The quality (10 to 100)
  applies to JPEG and JPEG 2000 and is greyed out for the other methods. The page tiles show the
  method each page will get. To keep memory use in bounds with large uncompressed pages, at most
  512 MB of prepared pages wait for being written.
* **JPEG 2000** is encoded with [OpenJPEG](https://www.openjpeg.org/), which the program already
  uses for reading JPEG 2000 scans. A quality value is meant to look about like JPEG with the
  same value (it's mapped to a target PSNR), and 100 is lossless. JPEG 2000 makes smaller files
  or better pictures than JPEG, but takes much longer, and some simple or old PDF programs can't
  show it; a PDF with JPEG 2000 states PDF version 1.5. To limit memory use, at most four images
  are encoded at the same time, each with a share of the processor cores.
* **JBIG2** makes black and white pages about a third smaller than CCITT G4, with exactly the
  same pixels. Only JBIG2's lossless "generic region" coding is used, never the symbol mode
  that stores similar-looking letters only once and can swap characters (such as 6 and 8).
  The arithmetic coder is taken from [jbig2enc](https://github.com/agl/jbig2enc) (Apache License
  2.0, see `src/core/jbig2enc`); no extra library is needed. CCITT G4 remains selectable for
  very old PDF programs.
* The compression options and "open the PDF after creating it" are kept as program settings,
  saved as soon as they are changed. By default
  the PDF is saved next to the project file and named after it. Replacing an existing PDF is
  always confirmed first, also when the program created it before, as it may have been changed
  since; only right after choosing it in the file dialog, which asked already, there's no
  second question.
* Several pages are prepared in parallel, with progress display and cancelling. The PDF is
  written to a temporary file first, so a failed or cancelled export never leaves a broken file
  behind or damages an existing one.
* **Text recognition (OCR)** with [Tesseract](https://github.com/tesseract-ocr/tesseract) adds
  an invisible text layer, so the text of the PDF can be searched, selected and copied. The page
  images stay unchanged; the text layer adds only a few kilobytes per page.
  * Several languages can be ticked at once (e.g. German and English). The text layout, which
    decides the order in which the text is recognized, can be detected automatically or set to a
    single column or a single block of text.
  * Pages with split output are recognized on their foreground, i.e. the text without the
    pictures.
  * Several pages are recognized in parallel (number of processor cores minus 2, at most 16).
  * Language files (`*.traineddata`, best from
    [tessdata_best](https://github.com/tesseract-ocr/tessdata_best)) are looked up in a
    `tessdata` folder next to the program, in a `tessdata` folder in the user's application
    data folder (on Windows `%APPDATA%\scantailor-ocr\scantailor-ocr\tessdata`) and in the folder
    `TESSDATA_PREFIX` points to; on Linux also in the system folders of the
    `tesseract-ocr-*` packages. Files put there by hand are found as well.
  * **More languages can be downloaded in the program** ("More languages ..."). The list comes
    from GitHub (tessdata_best, including the script models such as "Latin" or "Fraktur"), with
    search and file sizes. Several languages can be downloaded at once. Each file is checked
    against GitHub's checksum and only appears once it's complete. Downloads go to `tessdata`
    next to the program, or to the user's folder if the program folder can't be written to.
    The system's proxy settings are used. Without an internet connection, the installed
    languages can still be used.
  * Tesseract needs all languages of a run in one folder. If the ticked languages are in
    different folders, the missing files are copied into the user's folder once.
  * The invisible text uses Tesseract's "GlyphLessFont" (Apache License 2.0), embedded once per
    PDF.

### Deskew: oblique correction reworked

* The oblique (shear) correction is switched the same way as the deskew rotation now: two wide
  **Auto / Manual buttons** instead of a check box, both in the page options panel and in the
  default parameters dialog, in a consistent layout (heading, buttons, angle).
* **Fixed: the deskew "Auto" button lost its highlight.** All four mode buttons (deskew and
  oblique) were auto-exclusive inside one group box, and Qt groups such buttons per parent
  widget, so choosing an oblique mode silently unchecked the deskew mode. Each pair now has
  its own button group.
* Switching oblique to *Manual* resets an automatically found shear angle to 0, so the shear
  isn't left applied with automatic correction turned off.
* "Apply oblique automatically" acts as a master switch: stored per-page parameters can no
  longer re-enable oblique correction while it is off.
* The oblique finder reports *no* angle when the image has too little structure to tell an
  angle from noise, instead of shearing the page by an arbitrary amount.
* The "apply to other pages" dialog no longer accepts a selection that would apply neither
  the deskew nor the oblique angle.
* The drag handles for rotation and oblique correction sit closer to the image center, away from
  the edge of the view. How close is adjustable under *Settings → General → Deskew handle distance*
  (default 90 %).
* **Detection method per page.** "Top page edge" used to be one check box for the whole
  project. The line **Detection: [Content / Top page edge]** below Auto / Manual now sets it for
  each page; it is grayed out in Manual mode. Changing it measures the page again. **Apply to
  ...** transfers it with the deskew part, and the default parameters dialog sets it for new
  pages.
* **Apply to ... in Auto mode measures every target page on its own** (with the detection method
  applied) instead of copying the angle of the current page.
* Switching deskew back to *Auto* keeps a manually set oblique angle instead of resetting it to 0.
* Projects store the detection method per page (attribute `detection`). Older projects keep
  their project-wide setting, which is still written for other ScanTailor versions (it gets
  the method most pages use), so projects and profiles stay compatible both ways.

### New project

* The "Project Files" dialog has a third line, **Project File**. It suggests
  `<input folder>\<folder name>.ScanTailor` and follows the input folder until it is changed.
  The project is saved there as soon as the dialog is confirmed; an existing file is only
  overwritten after asking.
* With **Import new scans automatically from input folder** ticked, a project may start without any images. Such an empty
  project opens with the processing steps and the page list instead of the start page. Its
  project file keeps the input folder as a directory without files; other ScanTailor versions
  can't open a project without images, but open it normally once it contains one.

### Automatic import of new scans

* Step 1 is called **Import** now; fixing the orientation is one of its settings. Its new
  panel **Automatic Import** watches a folder, e.g. the one a scanning program saves to, and
  adds every new image to the end of the project. In step 1 the new scan is selected and
  shown; in the other steps it is only added, so it isn't processed right away with automatic
  settings. The project is saved after each scan.
* A new file is imported once it is completely written: its size and modification time
  stayed the same for a moment and the whole image can be loaded (in the background). A file
  that still can't be opened after a minute is reported and skipped. Subfolders such as `out`
  are not watched.
* The panel shows the folder (changeable) and is switched on with **Watch folder**. When
  it is switched on in a project, or another folder is chosen, the images already in the
  folder but not in the project can be chosen once, in the same lists as in the "Project
  Files" dialog. Importing is always off when a project is opened; it needs a saved project.
* **Insert after selected page:** the page selected when this mode is chosen is the anchor.
  Each new scan is inserted after the one before it and renamed to
  `<anchor name>_<scan name>.tif`, so it is sorted after the anchor in the folder, too. The
  program selects each new scan without moving the anchor; selecting another page (with the
  mouse or the keyboard) makes it the new anchor. If the anchor or the last inserted scan is
  removed, new scans go to the end again. A scan that can't be renamed keeps its name.
* **Replace selected page with next scan:** the next scan takes the place of the selected
  page (of both pages of a two-page scan) and is renamed to `<old name>_<scan name>.tif`. The
  page's settings and output files are dropped, and the old image is moved to the folder
  `replaced` next to it (with ` (2)` etc. added if the name is taken there). Then the mode
  switches to inserting after the new scan, where further scans usually belong.
  Several selected pages next to each other are replaced together: the new scan takes the
  place of the first and is named after it. The option shows how many pages are selected;
  with gaps in the selection it is disabled.
* Scans arriving during batch processing or while a PDF is created are added afterwards.
  Scans without DPI are added, too; the panel counts them, and their DPI can be set with
  Tools > Fix DPI of All Images.

### Image import

* **TIFF reading reworked**
  * Tiled TIFF files can be opened (previously, bi-level, grayscale and palette images failed).
  * Floating point, signed integer and 32-bit images are supported.
  * Colors of palette images in big-endian ("Motorola") TIFF files are correct now.
  * Fixed undefined behaviour when reading 2 and 4 bit images.
  * Read errors no longer yield images with uninitialised memory in the unread rows.
  * TIFF files using a compression the program can't decode are rejected right away when
    importing, naming the compression.
* **JPEG 2000 import** (`.jp2 .j2k .j2c .jpc .jpf .jpx .jph .jhc`) through OpenJPEG:
  fast import (only the file header is read), thumbnails decoded at reduced resolution,
  multi-threaded decoding, huge images decoded strip by strip to limit memory use.
* **Faster thumbnails for JPEG** files, decoded directly at reduced size.
* Fixed a leak and undefined behaviour in the JPEG metadata reader (libjpeg's error handling
  jumped past the cleanup of C++ objects) and a buffer bug in the PNG reader on partial reads.

### Output: split output with "Original background"

The *Original background* option of the split output (mixed mode) keeps the paper of the text
areas as scanned – with its tone and texture – under crisp black text, and writes this paper
layer to `out/original_background`. Three bugs, also present in the original project, kept it
from working:

* **Fixed a crash** when processing a page with this option and dewarping turned off: the
  binarized text was released from memory before its last use.
* **Fixed darkened pictures with black blobs:** the binarized text layer of the whole page was
  painted over the pictures, too. It is now limited to the text areas, with and without dewarping.
* **Fixed the paper tone being lost:** the fill zone step converted the text areas back to pure
  black and white, even on pages without fill zones, so the option had no visible effect.
  In this mode fill zones are now painted directly into the image.

### Output: grayscale output and pages without content

* **New option "Grayscale output"** in the output options and in the default parameters
  (profiles), for *Color / Grayscale* and *Mixed* mode. A color scan is then processed like a
  grayscale scan: the whole page in color mode, pictures and colored text in mixed mode come out
  in grayscale, with much smaller files. *Black and White* mode is not affected, as its color
  segmentation keeps colored text on purpose. The option is stored in the project only when
  it is turned on; other versions ignore it (and produce color output again).
  ([upstream #179](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/179))
* **Fixed a crash on mixed pages without content box** when the split output is turned on,
  and the message "1 output files could not be written" when it is off. Such pages now come out
  as white pages, with white split layers.
  ([upstream #172](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/172))

### Output: TIFF compression

* The compression of the output TIFF files moved from the settings window to a new panel
  **TIFF Compression** at the bottom of the output options, with a part for each kind of page
  like the PDF compression:
  * *Color and Grayscale*: None, LZW (default), Deflate, LZMA or JPEG, and the JPEG quality
    (default 85).
  * *Posterized Pages* (palette images, new setting): None, LZW (default), Deflate or LZMA.
    Before, they followed the color setting.
  * *Black and White*: None, LZW, Deflate, LZMA or CCITT G4 (default).

  Like the PDF options, they apply to all projects, are saved as soon as they are changed and
  are not stored in the project file. Previously chosen values are kept.
* **LZMA** makes the smallest lossless files, but is slow, and few other programs can read it;
  this program and its PDF export can.
* Smaller files without a setting: grayscale and color pages compressed with LZW, Deflate or
  LZMA use the horizontal *predictor* (smaller files with smooth pictures, readable everywhere), and
  posterized pages with up to 16 colors are stored with 4 bits per pixel instead of 8.
* After a change, the output of the open project is created again: the current page right away,
  the other pages during the next batch processing. Other projects keep their files until their
  pages are processed again.
* After processing, the output step shows the stored file instead of the computed image, as it
  already did for pages shown later without processing. So the losses of JPEG compression are
  visible right away, in the page view and in the thumbnail.
* Color JPEG is stored in YCbCr, the usual form of JPEG in TIFF, instead of RGB, which makes the
  files much smaller at the same quality.
* **Fixed:** posterized pages (palette images) couldn't be written with JPEG compression, as JPEG
  can't store palette images. They have their own setting now, without JPEG; images with an
  alpha channel are stored with LZW instead of JPEG.
* The PDF export compresses the pages itself, so these options don't change the PDF – except
  that JPEG-compressed TIFF files lose quality before they reach the PDF.

### Output: remove color noise

* **New filter "Remove color noise"** under *Output* → "Enhancement", the first of the
  filters (also in the default parameters dialog). Scanners and cameras often leave single red,
  green and blue pixels on gray paper, which the other filters keep, as they work on the
  brightness only. This filter splits the image into brightness and color (YCbCr, as in JPEG),
  blurs the color (*Radius*, 1–10 px) and turns colors weaker than the *Gray limit* (0–60)
  neutral gray, with a smooth transition up to twice that value; strong colors of pictures and
  colored text stay. The brightness is not changed, so text stays sharp. Only color images are
  affected. Not in ScanTailor Advanced or ScanTailor Experimental.
* **Faster enhancement filters.** The filters under "Enhancement" and the Gaussian blur, which
  is also used for binarization and dewarping, now spread their work over all processor cores.
  On a 24-megapixel color scan, removing color noise, evening out the background and the KNN
  denoiser together take 1.3 s instead of 5 s. The results are byte for byte the same.
* **With dewarping, the enhancement filters now work on the dewarped page** in output
  resolution, as without dewarping. Before, they filtered the whole original image in its own
  resolution – for a two-page scan both halves filtered the whole spread – so a radius meant a
  different size on paper with and without dewarping. Pages without these filters are
  unchanged.

### Output: even out letter widths after dewarping

* **New option "Even out letter widths"** under *Output* → "Dewarping", below *Depth
  perception* (also in the default parameters dialog), off by default. Dewarping straightens
  the lines well, but the letters often come out too narrow near the spine and too wide further
  away – on a real book page from −18 % to +13 %. *Depth perception* only scales this
  correction as a whole, so a higher value fixes the spine and overstretches the rest.
* With the option on, the dewarped page is measured: the vertical stems of letters have the
  same width everywhere on a printed page, while horizontal strokes are not affected by a
  horizontal compression, so their ratio shows the local scale. It is measured in windows of
  about 7 mm. From this profile a correction is computed that stretches the compressed and
  compresses the overstretched areas, keeping the page width and margins; then the page is
  dewarped once more. On the real book page the deviations dropped to −9 % at the spine and
  ±2 % elsewhere; synthetic pages with known geometry and a flat control page were used to
  check the measurement.
* No correction for pages with too little text, deviations below 3 % or windows with clearly
  less text than usual (the ragged end of lines); at most 40 % in either direction.
* The correction is stored with the distortion model in the project, so zones and the views
  use the same mapping as the output; moving a curve by hand discards it. Dewarping takes about
  twice as long for pages with the option on. Not in ScanTailor Advanced or ScanTailor
  Experimental.

### Options panels: one layout for all steps

* Each panel holds what its **Apply to ...** button applies to other pages. Parts of a panel are
  divided by bold headings instead of nested collapsible boxes, and **Apply to ...** is the last
  line of the panel. Its tooltip says what is applied. The buttons have the same width in all
  steps, and the dialogs they open have distinct titles.
* *Import*: "Rotate" and "Crop scan (trim)" are one panel, **Rotate and Trim**, as the button
  applied both anyway. The "Reset" and "Reset trim" buttons are gone: the arrows rotate back, and
  the check box turns the trim off.
* *Split Pages*: the page type is switched with **Auto / Manual** buttons in the panel, as is the
  split line. Before, going back to automatic detection was only possible in the dialog behind
  "Change ...". That dialog is now an **Apply to ...** dialog with two options: *Page type* and
  *Split line* (formerly "Apply cut": the position of the split line, adapted to each page).
  The pictures of the four page layout buttons are redrawn: white pages with a frame, the offcut
  mid gray with a dashed edge and as wide as the page. They keep their colors in the dark color
  scheme. The buttons are 42 x 42 pixels, without text; the tooltip names the layout.
* *Deskew*: the panel **Deskew and Oblique** has the headings "Deskew" and "Oblique". The options
  of its **Apply to ...** dialog are named like them, *Deskew* and *Oblique* (formerly "Deskew
  angle and mode" and "Oblique angle and mode").
* *Select Content*: "Page Box" and "Content Box" are one panel, **Page and Content Box**.
* *Page Layout*: the button "Match size to all pages" is gone. It applied the whole alignment
  (not only the matched size) to all pages, like **Apply to ...** with all pages, and didn't
  store the check box "Match size with other pages" for the current page. The tooltip of
  **Apply to ...** says what is applied. The nine alignment buttons are centered, also in the
  Default Parameters window.
* *Output*: the panel **Mode** has the headings "General" (formerly "Options"), "Enhancement",
  "Filling", "Threshold", "Color Operations" and "Picture Zones" (formerly "Picture Shape", like
  the tab in which picture zones are edited). The collapsed state saved for the old nested boxes
  is no longer used. "Fill offcut", "Fill outside page box" and "Fill margins" are under
  "Filling" with the color, the two smoothing options (black and white only) under "Threshold";
  the same in the Default Parameters window, where "Threshold" now comes before "Color
  Operations" as in the Output step. There, the smoothing options follow right below the
  settings of the chosen threshold method, and the window is measured once its tabs are fully
  set up, so the Output tab no longer gets a horizontal scroll bar.
* *Output*: "Equalize illumination (B&W)" and "Equalize illumination (Color)" are one check box,
  "Equalize illumination", for the setting the color mode uses, with "Also in picture zones"
  below it for mixed mode (both settings are stored as before). In the Default Parameters
  window, "Also in picture zones" now also needs "Equalize illumination", as in the Output step.
* *Output*: the panel "Processing" is called **Content** and comes right after **Output
  Resolution**; its check box "Black on white mode" is called "Dark content on a light
  background". The tooltips say that the page is inverted when it is off and that **Apply to
  ...** sets the value by hand on the other pages, so the automatic detection no longer changes
  it there.
* *Output*: tooltips for "Fill offcut", "Fill margins", the filling color, both smoothing
  options, the threshold method, the picture zones, "Higher search sensitivity", "Despeckle",
  "Split output" and the two kinds of foreground, also in the Default Parameters window.
* **Fixed: changing the output resolution turned off automatic dewarping** on pages not yet
  processed in the Output step. A page with dewarping "Auto" switches to "Manual" when its
  resolution changes, so it keeps the curves found; without curves found yet, "Manual" meant no
  dewarping at all. Such pages now stay on "Auto". The tooltip of the resolution says so.
* **Fixed: the filling color "Black" was ignored with dewarping**; the filled areas got the
  background color (color mode) or white (mixed mode). Dewarped pages now use the same filling
  colors as the others. The tooltip says that "Background" gives white in mixed mode.
* *Output*: the **output resolution** (a list with 300, 400, 600 and 1200 DPI; other values can
  be typed in) and the **dewarping** mode with "Post deskew" are set in the panels and apply to
  the current page at once, like all other settings. "Change ..." became **Apply to ...**. The
  angle found by "Post deskew" is shown next to it.
* All "apply to other pages" dialogs offer the same seven choices, also in the Output step
  (formerly four there), and those that apply only some of the settings list them below the
  choice of pages under "Apply Parameters".
* *Create PDF*: "Open the PDF after creating it" sits next to the **Create PDF** button, as it
  concerns creating rather than the content of the PDF. The lists of the compression panels
  (also of *TIFF Compression*) keep their width and line up instead of filling a widened panel.
* *Import*: the button **Change ...** of **Automatic Import** sits next to **Watch folder**, and
  the folder is shown below at full width; the line "Folder:" is gone.
* *Output*: **Wiener denoiser** has a check box. Below it, its two values are named, *Strength*
  (0.01 to 1) and *Window size* (now 3 to 99). Turned off, the strength is stored as 0, as
  before; turned on again, the last strength is used (0.10 at first). The three values of
  **Color segmentation** have the headings *Red*, *Green* and *Blue* above them and a normal
  frame. The four dewarping buttons are arranged two by two (Off, Auto / Marginal, Manual), so they
  don't make the panel wider.
* *Margins*: the source DPI has its own panel, **Source Resolution**, above **Margins**, whose
  **Apply to ...** never applied it. "Resolution:" has two lists (horizontal × vertical) with
  300, 400, 600 and 1200 dpi that also take any other value, typed with or without "dpi"; the
  separate list of presets is gone. A value chosen from a list sets both while they are the
  same; typing changes only the one field. The button "Fix DPI of all images ..." (formerly
  "Fix all ...") opens the same window as *Tools > Fix DPI of All Images ...* (formerly "Fix
  DPI ...").
* *Fix DPI of All Images*: the resolution is set like in **Source Resolution**, with two lists
  "Resolution: [300 dpi] × [300 dpi]" instead of a list of presets and two plain text fields;
  the button is called "Apply to selection". The window opens larger, and the list of images
  takes the extra height.
* *Fix DPI of All Images*: several images or size groups can be selected with Ctrl and Shift,
  and "Apply to selection" sets the resolution of all of them. The fields show the resolution
  if all selected images have the same one; the entered value is checked against every
  selected pixel size.
* If an image file of the project is replaced by one with a different pixel size (a new scan),
  its resolution is taken over as well, as long as it is plausible (the resolution field
  would not be marked red). Otherwise the stored resolution stays, as it may have been
  corrected by hand.
* The resolution fields (*Margins*, *Output*, *Default Parameters*, *Fix DPI of All Images*) are
  wide enough for "1200 dpi".
* *Margins*: the panel **Output Size** (between **Margins** and **Alignment**) shows the common
  size of the pages whose size is matched, in the current units. "Lock aggregate size for
  matching" is replaced by an own target size, see "Scaling pages to the output size" under
  "Adopted from ScanTailor Experimental".
* The options panels are at least 320 pixels wide (formerly 274), so a scroll bar doesn't cover
  their content.
* Status bar: the file name shows up to 50 characters (formerly 15, with split pages 11); the
  full name is in the tooltip. The image size is followed by the resolution of the image shown,
  e.g. "210 x 297 mm / 300 dpi" (in *Output* the output resolution), so a wrong source DPI can
  be seen in every step.
* Settings that don't apply at the moment are grayed out instead of hidden, so the panels don't
  jump: in *Split Pages* the split line of a single page, in *Select Content* the fine tuning
  and the size of the page box, in *Output* the parts that the color mode doesn't use
  (threshold, picture zones, splitting, despeckling, filling and some general options), the
  picture zone sensitivity, and the depth perception while there is no dewarping. The Mode and
  Despeckling panels and the depth perception are shown in every view of the Output step.
* *Create PDF*: the options have the same margins as in the other steps, and **More languages
  ...** sits next to **Recognize text**, as **Change ...** next to **Watch folder**; it can be
  used while text recognition is off.
* *Output*: **Wiener denoiser** has its own heading, "Enhancement" (formerly "Denoising"), right
  after "General" (also in the default parameters dialog), instead of being under "Color
  Operations": it reduces the noise of the image before all other processing, in every color
  mode.
* *Output*: the sliders **Depth perception** and **Despeckle** show their value above them, as
  the Otsu slider does (also in the default parameters dialog). Moving the depth perception
  slider now recalculates the page once it is released; before, the page was only marked as
  outdated after switching to another page. On the Dewarping tab the preview follows the slider
  as before.
* One alignment in all options panels (also in the default parameters dialog): check boxes,
  option buttons and "label: field" lines start at the left edge of the panel. What depends on
  a check box is indented below it, one step further for each level (e.g. the modes of
  **Watch folder**, the trim values, "Original background" below "B&W foreground", the
  languages of **Recognize text**). What depends on Auto / Manual buttons or a list is grayed
  out, but not indented. Values have their label on the same line ("Angle:", "Width:",
  "Top:") and the unit in the field (°, px, %, or the chosen unit of measurement).
* *Import*: the trim values are named *Top*, *Bottom*, *Left* and *Right*, as the margins in
  *Margins*, instead of the signs next to the fields.
* *Deskew*: the angle comes right below Auto / Manual, before "Detection:". Both angles
  are named *Angle*, have the degree sign in the field, are left-aligned and equally wide.
* *Output*: the output resolution is chosen after **Resolution:** in the panel **Output Resolution**
  (formerly "Output Resolution (DPI)"), and its list shows the unit ("300 dpi"; typed values are
  read with or without it). The color mode has its own bold heading, **Color Mode**.
* The titles of all options panels (and of the panels in the default parameters dialog) are
  bold.
* *Output*: the depth perception, which only the dewarping uses, is part of the **Dewarping**
  panel (also in the default parameters dialog), and its **Apply to ...** applies it together
  with the dewarping mode. Its own panel and dialog are gone.
* Native color scheme on Windows 11: the arrows of number fields are placed above each other
  instead of next to each other, so the fields are as narrow as in the other color schemes and
  rows of them (e.g. the trim values) fit into the panels.
* Nothing changes in the processing or in the project file.

### Consistent wording

* Panel titles, headings, window titles and menu entries are written in title case throughout,
  e.g. **Page and Content Box**, **TIFF Compression** or *Tools → Default Parameters ...*. Check
  boxes, buttons and labels keep sentence case, except the buttons **Apply to ...**. A "..." is
  always preceded by a space.
* The check box of the panel **Automatic Import** is called **Watch folder**.
* *Output*: the dialogs of the panels Mode, Splitting and Processing are called "Apply Mode",
  "Apply Splitting" and "Apply Processing", like the dialogs of the other panels.
* *Settings*: "Params" became "Parameters". *Default Parameters*: the first tab is called
  "Import" like the step.
* *Output*, **Picture Zones**: the shapes are called "Contour mask" and "Rectangle" (formerly "Free"
  and "Rectangular").
* "Folder" instead of "directory", as in Windows, e.g. **Input Folder** and **Output Folder** in
  the "Project Files" dialog.
* "Project Files" dialog: after moving files to the other list, the next file is selected, so
  the arrow button can be clicked repeatedly without selecting a file each time.
* German translation: the same thing is called the same everywhere (e.g. "Schwarz-Weiß",
  "Geraderichten", "Ordner"), and "..." is preceded by a space.
* *Output*, Wolf threshold: "Lower bound:", "Upper bound:" and "Coef:" as with Sauvola (formerly
  "Upper Bound:" and "Coeff:").
* Check boxes in sentence case also in *Select Content* and *Margins*: "Fine-tune page corners"
  and "Auto margins". "Resolution:" has a colon like the other labels, "Color:" (*Output*,
  Filling) no longer a trailing space. *Deskew*: the detection method is called "Top page edge";
  that it is meant for book scans with a dark background stays in the tooltip.
* *Select Content*: the page and content box mode is "Off" (formerly the verb "Disable"), as in
  the Output step.
* Tooltips for the rotate buttons (*Import*), the split line modes (*Split Pages*), the deskew
  modes (*Deskew*) and the page and content box modes (*Select Content*), also in the Default
  Parameters. "Fine-tune page corners" explains that it shrinks the page box from each dark
  corner.
* *Settings*: labels without a trailing space; "Use auto detection in the Output step" and its
  tooltip name the current check box of the Output step ("Dark content on a light background").
* File dialogs offer "ScanTailor OCR Projects"; the menu entry is "Go to Page ...".

### Default parameters dialog

* Each tab is laid out like the options panel of its step: the same panel titles, bold headings
  instead of nested boxes, the same buttons and names. *Import*: "Reset" is gone, as in the step.
  *Split Pages*: **Auto / Manual** buttons instead of a list. *Deskew*: **Deskew and Oblique**.
  *Select Content*: **Page and Content Box**. *Output*: the output resolution is an editable list
  (72 to 1200 DPI), dewarping uses the four buttons of the Output step, and color segmentation
  shows *Red*, *Green* and *Blue* above its fields.
* New in the dialog, as in the Output step: **Wiener denoiser**, **Fill outside page box**, the
  threshold methods **Fox** and **Window**, and **Delta** for Sauvola and Wolf (the same value as
  the Otsu slider). Profiles stored these values before, but the dialog always reset them.
* The window opens large enough to show every tab but *Output* without scrolling, at most as
  large as the screen. *Output*, much taller with its filters, gets one and a half times the
  height of the tallest other tab and scrolls beyond that; its group *Filling* moved to the
  right column, so that both columns are about equally long. The window can be made lower, but
  not narrower, so only the height ever scrolls.
* Fixed: the threshold methods Bradley, Grad, EdgePlus, BlurDiv and EdgeDiv showed the settings
  of another method; and the upper bound of the Wolf method was not saved in a profile.
* Fixed: the deskew angle of the *Deskew* tab took only 0 to 99.99 degrees; it now takes -45 to
  45 degrees, as in the Deskew step.

### Zone editors (picture zones and fill zones)

* The selection mode for new zones is shown at the bottom right of the status bar as a button
  with icon and name ("Polygon selection", "Lasso selection", "Rectangle selection").
  Clicking it opens a menu to change the mode; the keys Z, X and C still work, and the last
  used mode is kept, also across restarts.
  ([upstream #15](https://github.com/ScanTailor-Advanced/scantailor-advanced/issues/15))
* The hints in the status bar while drawing a zone explain each mode, including the lasso,
  and follow the mouse: the "click to finish" hint no longer stays after moving away from the
  starting point.

### Translations

* Qt's own texts – standard buttons such as "Cancel", parts of the file dialogs, the context
  menu of text fields – are translated now as well, into the language chosen in the settings.
  The Windows ZIP ships Qt's translations for the program's languages in `translations`; on
  Linux they come from the system (`qt6-translations-l10n`, recommended by the `.deb`).

### Thumbnails and page selection

* Selected thumbnails have a blue border, so the selection stands out in all colour schemes.
  Only the current page is also filled, with a stronger grey than before, so it can be told
  apart from the other selected pages at a glance.
* The context menu of a thumbnail has a submenu "Select" to select pages by pattern, counted
  from the page clicked: all pages, this page and the following ones, every other page (odd or
  even pages, as named in the menu), this page and every other following page, every other
  selected page, and this page only. The selection can then be adjusted with Ctrl and Shift.
* When several pages are selected, the "Apply to ..." windows open with "Selected pages"
  chosen.
* All "Apply to ..." windows share one page choice. Each entry shows how many pages it applies
  to, and "Every other page" says whether the odd or even pages are meant.
* *Import*: "Apply Rotation and Trim" lets you choose to apply only the rotation or only the
  trim, like the other "Apply to ..." windows.
* "Every other selected page" now works the same in all steps: every other page of all
  selected pages, before and after the current page, which is included. Before, some steps
  only counted onwards from the current page, others only looked at the first block of
  adjacent selected pages and applied nothing if the current page wasn't in it.

### Accent color

* Settings > "Accent color": a color picker for the accent color of the light and dark color
  schemes. It replaces the blue of the borders of hovered and focused controls, of the selected
  thumbnails and of the progress bar; the background of selected text keeps its color.
  "Default" goes back to the blue of the color scheme. The setting takes effect after a
  restart and isn't used by the native color scheme, which takes the colors of the system.

### Start page

* "New Project" and "Open Project" are buttons now instead of blue links.
* Recent projects are listed with their name in the normal text colour and their folder
  below in smaller, slightly faded text; an entry is highlighted in grey under the mouse and
  opens on click or with Enter/Space. The full path is shown as a tooltip.
* The start page has the window background, so its text is no longer drawn on the mid-grey
  image area of the light colour scheme.
* The buttons "New Project" and "Open Project" look like the other buttons of the program.
  "Recent Projects" is a bold title centred on its frame line in all colour schemes; the
  project names are bold and one point larger than the normal text, the folders normal.

### Error reporting

* When images can't be loaded or output files can't be written, the reason is shown –
  collected in a single, non-modal message instead of one message per file.
* An unexpected error while processing a page no longer terminates the program without a
  message. The page shows the error with its reason instead of the image, the failure is
  listed in the same collected message, and batch processing continues with the other pages.
  Errors in the user interface are reported with a message as well. (Hard crashes, such as
  memory access violations, still can't be caught this way; they have to be fixed one by one.)
* Output TIFF files are checked to have been written completely (e.g. on a full disk);
  a failed write no longer leaves a damaged file behind.
* Malformed values in project files (e.g. a missing binarisation threshold) no longer turn
  into settings that black out a page.

### Adopted from ScanTailor Experimental

Ideas and fixes taken over from
[ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental),
a separate fork of the original ScanTailor. Its code is structured differently, so they were
reimplemented rather than copied.

* **Scaling pages to the output size** in *Margins* (ScanTailor Experimental: "match size
  mode"). Pages scanned at different sizes, e.g. inserted sheets or pages from another copy, no
  longer sit small in wide margins. The panel **Output Size** has two parts:
  * *This Page*: "Match size" replaces the check box "Match size with other pages": **Off**
    (the page keeps its own size), **Fill with margins** (as before) or **Scale**: the content is
    enlarged or reduced to fit the output size, the margins keep their size in millimeters, and
    the rest is filled with margins as aligned. "Factor" shows how much the content of the page
    is scaled. The choice has its own **Apply to ...**; the one of **Alignment** applies only
    the modes and the position now.
  * *All Pages*: the common size (the largest width and height of the matched pages, as
    before), "Own target size" with width × height for the output size (it starts with the
    common size; it replaces "Lock for now" and is kept in the project), and the limits
    "Enlarge to at most" (150 %) and "Reduce to at most" (50 %). Pages filled with margins are
    never enlarged, but reduced when they are larger than an own target size.

  Only the content is scaled, before binarization, so text stays smooth; the output files keep
  the resolution set. Picture zones, fill zones and dewarping curves follow by themselves. The
  page view of *Margins* shows a scaled page as it will look in the output, and the page size in
  the status bar is the output size. The Default Parameters window sets "Match size" for new
  pages and the own target size and the limits for new projects (a target size in pixels refers
  to the output resolution there). Projects that use none of it give exactly the same output as before. Unlike ScanTailor
  Experimental, there is no separate scaling of width and height (it would distort the text).

* **Large images load again.** Qt 6 refuses to decode images that need more than 256 MB, so
  large color JPEG or PNG scans (e.g. 11000 × 8500 pixels) could not be loaded. The limit is
  lifted. TIFF and JPEG 2000 were not affected.
* **New filter "Even out background"** (in ScanTailor Experimental: "Subtract background") under
  *Output* → "Enhancement" (also in the default parameters dialog). It evens out the paper
  background (stains, uneven yellowing, shadows near the spine) before the other processing:
  the background is estimated with a median filter and raised to the brightness of its brightest
  part. *Strength* (0.01–1) and *Radius* (5–300 px) can be set; the radius should be clearly
  larger than the letters. Pictures are flattened as well. Unlike in ScanTailor Experimental,
  the median is computed on a reduced image, which keeps large radii fast, and the paper is not
  darkened to a middle gray. The values are stored in the project only when the filter is on.
  * New, not in ScanTailor Experimental: **"Make paper white"**. The filter normally works on
    the brightness only, so evenly yellowed paper stays yellow in color output. With this check
    box each color channel is divided by its own background, which takes the paper to white and
    removes the color cast; dark text stays dark, the colors of pictures shift as well.
* **New filters "KNN denoiser" and "Sharpen"** under *Output* → "Enhancement" (also in the
  default parameters dialog), each with *Strength* and *Radius*. The KNN denoiser averages each
  pixel with growing windows around it, weighted so that windows reaching across an edge count
  little: it reduces noise and keeps the edges of letters sharp (strength 0.01–1, radius
  1–20 px; unlike in ScanTailor Experimental the windows are symmetric). "Sharpen" is an unsharp
  mask (strength 0.1–3, radius 1–10 px), not in ScanTailor Experimental in this form; there,
  a negative blur does the same. The filters run in the order of the panel: even out
  background, KNN denoiser, Wiener denoiser, sharpen – smoothing first, so that sharpening does
  not amplify the noise. In color they work on the brightness, colors keep their hue. Values
  are stored in the project only when a filter is on.
* **Five new binarization methods** in the method list under *Output* → "Black and white" (also
  in the default parameters dialog): **Gatos** (estimates the paper first; for yellowed,
  stained or unevenly lit pages), **Nick** (little noise on empty paper; for faint print),
  **Singh** (mean and local contrast, fast), **Robust** (dark letters against a brighter
  surround, hardly affected by shadows near the spine) and **MultiScale** (several window sizes;
  for text of very different sizes). Each has a window size and a strength. Unlike the features
  above, their code is taken over unchanged (`imageproc/ExperimentalBinarization.cpp`), and
  gives the same result pixel for pixel. Switching to or from one of them sets the window size
  and strength to the values of the new method, as ScanTailor Experimental does, since they
  differ widely (window 40 for Robust, strength 0.1 for Nick). Every method in the list now has
  a tooltip saying what it suits. The other methods of ScanTailor Experimental were compared on
  sample pages and left out: they hardly differed from these or from the existing ones.
* **Settings for all binarization methods** under *Output* → "Black and white", below the
  settings of the chosen method (also in the default parameters dialog):
  * **"Colored"** (ScanTailor Experimental: "dimming colored") darkens colored pixels before the
    conversion to black and white, the more the more saturated they are, so that colored text
    such as red headings comes out black. New, not in ScanTailor Experimental: negative values
    lighten colored pixels instead, so that colored stamps, underlinings or highlighter marks
    disappear from black and white pages. Gray pixels are never changed; 0 changes nothing.
  * **"Bounds"**: pixels darker than the lower bound always become black, pixels lighter than
    the upper bound always white. They were there before, but only for Wolf, Fox, Window and
    Grad; now they apply to every method. The default bounds 1 and 254 leave the other methods
    alone, so older projects keep their results.
  * With this, **Otsu shows a "Delta" field** like the other methods instead of its slider (the
    same value, -100 to 100: positive values make the letters thicker, negative ones thinner),
    and the labels and fields of all methods line up with "Colored" and "Bounds" below them.
* **K-means for colored text** under *Output* → "Color Operations" (also in the default
  parameters dialog). The new list "Reduce colors" replaces the check box "Posterize". On color
  pages it offers None and Posterize for the whole page, as before. On black and white or mixed
  pages, with color segmentation on, it chooses how the text is colored: None (color segmentation, each letter in its
  average color), Posterize (the same, then the grid of fixed color steps, as before) or **K-means**, which finds
  the colors that fit the page best: "Colors" (2 to 16, default 6) and "Fixed black" (one color is
  pure black, so black text gets no tint). Each letter, a connected area up to a filled square of a
  quarter inch, gets one color for each of its segments from the color segmentation, so touching
  letters of different colors stay apart, and "Reduce noise" and the red, green and blue settings
  apply as without K-means; larger areas, such as pictures and the lines of drawings, are colored
  pixel by pixel and keep their shading, whatever the segmentation settings. The idea
  is from ScanTailor Experimental; the code is our own, much shorter and about five times faster,
  and groups the colors in CIE Lab space so that distances follow what the eye sees. In a
  comparison on 100 sample pages with posterizing and with the code of ScanTailor Experimental, it
  was chosen as the best balance of quality and file size. The posterizing of the text and that
  of whole color pages are now stored apart, so that switching a page from black and white to color doesn't posterize it unnoticed; older projects
  keep their result.
* **Double-click shows the whole page again** after zooming in, in the image views of all steps
  except *Select Content* and *Margins*, where a double click already corrects the content box
  or attaches the content to the nearest guide. The status bar hint says so.
* **The automatic content detection** in *Select Content* binarizes the page with BlurDiv
  instead of the Wolf threshold. BlurDiv evens out the paper first, so shadows at the spine and
  yellowed edges are less often taken for content. Content boxes already set are not changed.
* **Deliberately not adopted: "unPaper" and "Balance".** unPaper sets the paper to one color
  for the whole page (yellowed paper stays yellow, shadows and gradients stay), and pale
  colored areas can be flattened with it; Balance chains four overlay blends of blurred copies
  on the brightness only. "Make paper white" removes the color cast and follows shadows and
  gradients, and the denoisers smooth the paper texture.
* **Deliberately not adopted: the "horizontal barreling" correction of the dewarping.** On
  synthetic test pages with known geometry (bent, keystoned and barreled) and on a real book
  page it brought no measurable improvement: the straightness of the lines changed by less
  than 0.2 px at 600 dpi. Its actual barreling term was tiny; what it mainly changed was a
  weighting factor taken from the perspective of the page, which jumps between 0 and 1 on
  similar flatbed scans and weakens the curvature correction by up to a third without a clear
  reason.

### Adopted from ScanTailor Spectre

Ideas and fixes taken over from
[ScanTailor Spectre](https://github.com/abandoned-industries/scantailor-spectre), a macOS fork of
ScanTailor Advanced.

* **Settings for the automatic content box detection** in *Select Content*, for art books and
  photo pages (also in the default parameters dialog):
  * "Fill factor" (30–100 %, default 65 %): the highest share of black pixels a line may have to
    count as text. Above 85 %, shadows are no longer subtracted and pictures count as content;
    from 95 % on, the whole page box is the content box.
  * "Border tolerance" (0–100 %, default 0 %): how much of the content touching the image border
    is kept, e.g. pictures that reach the edge of the page.

  The settings are program settings, not saved in the project. Changing them detects the content
  box of the current page again; other pages get the new settings when their content box is
  applied to them with "Apply To ..." (or detected again for another reason).
  The defaults give exactly the detection as before; ScanTailor Spectre's border tolerance
  changes it even at its default.
  Its additional detection of photo plates relies on Apple Vision and was not adopted.
* **Color mode detection** in *Output*. The color mode is now chosen with three picture buttons
  instead of a list, so the active one is visible at a glance: black text with a black picture
  in the top right corner for Black and White, colored lines that turn gray below a diagonal for Color / Grayscale, a colored bar above
  black text for Mixed. They have no text (the tooltip names the mode), are 42 x 42 pixels like
  the page layout buttons of *Split Pages*, and keep their colors in the dark color scheme. In
  the panel, the new group "Color Mode Detection" comes before the Mode group; in the default
  parameters dialog it is a group of its own above Output Resolution. It detects the color mode
  of chosen pages ("Detect ...", with the usual page selection) and sets it. It takes the geometry from the steps before, as the
  thumbnails do, and only loads the source images, so it is fast and creates no output files.
  Pages whose earlier steps have not been done yet are skipped. The project file stores only the
  color mode, as before.

  The detection of ScanTailor Spectre (based on Leptonica, which comes with Tesseract) was
  extended after measurements on a grayscale-scanned book with photos and a color atlas:
  * "Color from" (default 3 %): a page counts as a color page only if enough of it is colored
    outside pictures, so colored headings, boxes and thumb index tabs alone don't make a color
    page.
  * Color only in pictures makes the page Mixed; a grayscale page with text beside the picture,
    such as a caption, is Mixed as well.
  * "Also look for grayscale pictures" with "Pictures from" (default 5 %): our picture zone
    detection as a second opinion finds light, small or very dark photos that the Spectre
    detection misses. Only zones that look like pictures count (mostly dark, or evenly gray), so
    dense text and empty paper are not taken for pictures.

  The three settings apply to all projects and are also in the default parameters dialog.
  "Apply to ..." of the Mode group can now apply the color mode and the other settings of the
  group separately. Without OCR, the detection is not available.

  The thumbnails of *Output* can be ordered by color mode ("Order by color mode": black and
  white, color / grayscale, mixed, each in natural order), also for pages not processed yet, to
  check the detected color modes page by page. This order, "Order by completeness" and "Order by
  split type" explain their order in a tooltip.

* **Photo adjustments** in *Output*: the new group "Photo Adjustments" after "Dewarping" (the
  adjustments come after dewarping and despeckling) has the
  sliders of a photo editor, each with a field for the exact value: *Temperature* and *Tint*
  (color balance), *Exposure* (−2 to +2 EV), *Contrast*, *Highlights*, *Shadows*, *Whites* and
  *Blacks*. The page is calculated again shortly after a slider is released. Unlike ScanTailor
  Spectre, the adjustments are applied to the finished output instead of the source image, so
  black and white text, picture detection, color mode detection and OCR stay as they were:
  * Color / Grayscale: the whole page; the margins are never changed. "Keep white" (off by
    default) also keeps pure white inside the page, such as a background made white.
  * Mixed: only the picture zones; the text stays black and white.
  * Black and White: not used (the group is grayed out). The color balance is grayed out with
    "Grayscale output".

  The values are saved per page in the project file, only for pages that use them, and have
  their own "Apply to ...". As in Lightroom,
  positive *Highlights* make the bright parts brighter (ScanTailor Spectre does the opposite).
  The automatic suggestion of ScanTailor Spectre was not adopted: it measures the whole source
  image, including the second page and the book edges, instead of the pictures.

* **Seed fill propagates completely.** The seed fill used for picture detection, background
  estimation and despeckling marked a pixel as "queued" and never cleared the mark after
  processing it. If the pixel was improved again later, the improvement no longer spread to its
  neighbors. A second fix: one neighbor in the 8-connected fill used the wrong row stride.
  Results change only marginally (a few pixels at letter edges on some pages).
* **Faster grayscale conversion.** Converting 32-bit color images to grayscale, and computing
  their histograms, now reads the image line by line instead of pixel by pixel. Unlike in
  ScanTailor Spectre, only formats whose stored values are identical are read directly, so the
  output stays byte-identical.

### Correctness and robustness

* **Fixed outdated thumbnails** on Windows when the path of a thumbnail is longer than 260
  characters, as with pages whose names grew with each scan inserted after them: the new
  thumbnail couldn't replace the old one, which then kept being shown, also in the PDF step.
  Replacing files now works with long paths, which also applies to saving the project file.
* **Fixed: changing the source DPI in the Margins panel had no visible effect** until another
  step was chosen. The page list keeps its own copy of the DPI, which the processing uses; it
  is now updated right away, as after *Tools → Fix DPI of All Images*.
* Radio buttons are round in the light and dark color schemes at every font size. With the
  usual Windows font size, unselected ones were drawn as squares: after rounding the size to
  whole pixels, the corner radius was slightly more than half of it, and Qt then drops it.
* Fixed several defects that produced wrong results or crashes: a grayscale measurement that
  read the wrong image, a division by zero in Wolf binarisation on blank pages, integer
  overflows in pixel arithmetic (TIFF buffers, binarisation, distance transform) on large
  images, missing guards in `BinaryImage`, a wrong assertion and a lost search direction in
  the arc length mapper, an unsigned wraparound in the page split gap scan, and divisions by
  zero in the content finder and with missing resolution (DPI) information.
* Fixed a use-after-free when finishing a lasso zone, and thread-safety issues with the
  application settings, the default parameter profiles and the deviation statistics used for
  sorting thumbnails.
* Natural file name sorting now also compares the separators, so names differing only in
  those (`img-2.tif` vs. `img_1.tif`) sort by the whole name rather than falling back to a
  plain lexicographic compare. **This can change page order in projects with inconsistent
  file naming.**
* **Translations work again.** Since the executable was renamed to `scantailor-advanced`, the
  program looked for translation files with the old name and found none, so it always ran in
  English. It now finds them. On first start it also picks the translation for the system
  language when only the language matches (e.g. `de` for `de_DE`). The German translation is
  complete again, including all texts added in this fork.
* The thumbnail list keeps its position when re-sorting moves the current page elsewhere.
* Auto-save is also triggered by changes to the page list and to the current page.
* If the output folder has been deleted (e.g. to output everything again), opening the project
  creates it again instead of asking for relinking, as long as the folder it belongs into and
  all images of the project are still there. Otherwise the project has probably been moved to
  another place or computer, and relinking is offered as before.
* Switching back to a step after batch processing or after "Create PDF" no longer connects the
  signals of its options panel a second time.
* *Fix DPI of All Images* no longer loses its changes when the project file is in the same
  folder as the images (the image paths contained "/./" and didn't match), and gives each page
  of a multi-page TIFF its own resolution instead of the one of the next page.
* After switching to another window with Alt+Tab and back, the drag handles (split line,
  deskew, content box, margins) and the zone editors react to the mouse again; before, Alt
  counted as still pressed until another page was shown.
* Code cleanups based on the compiler's static code analysis.

### Build and tests

* CMake verifies which compression schemes libtiff supports (LZMA etc. are required).
* Optional static code analysis: `-DENABLE_CODE_ANALYSIS=ON` (MSVC: `/W4 /analyze`).
* Optional developer tool `binarization_compare` (`-DBUILD_DEV_TOOLS=ON`): binarizes sample
  pages with our methods and those of ScanTailor Experimental and writes a web page that shows
  the same part of the page for every method side by side, with zoom and computing times.
  Call it as `binarization_compare <output folder> <image file or folder>...`; pages already
  computed are only computed again when the image changed, so sample pages can be added later.
  The code of ScanTailor Experimental it uses is kept unchanged in
  `src/tools/binarization_compare/experimental` (left out of the formatting check).
* New tests for the image readers and the TIFF writer; CI now fails on failing tests and also
  builds on Windows.
* New tests for the PDF export: file structure and cross-reference offsets, page size from the
  resolution, lossless G4 and JBIG2 round trips (JBIG2 with a decoder written from the standard
  for the tests, and on Linux additionally rendered by Poppler's `pdftoppm` and compared pixel
  by pixel), cropping of split backgrounds, page order, text layer,
  finding and gathering language files, download checksums.
* CI installs Tesseract on Linux and Windows.
* Qt 5 is no longer supported; the program needs Qt 6.4 or later (the version of Ubuntu 24.04).
  The code paths for older Qt versions are removed. CI builds Linux with Qt 6; the Linux
  packages (DEB, AppImage) are built with Qt 6, like the Windows version. The Windows job keeps vcpkg's intermediate files off the
  runner's small system drive and only saves a new package cache when something was rebuilt.
* The Lint workflow uses clang-format 22.1.3, the version that comes with Visual Studio 2026, so
  that a local check gives the same result (other versions format some lines differently).
* No Flatpak: upstream's manifest is removed, as the Flatpak runtime has no Tesseract and the
  package was not published anywhere. The Linux packages are the .deb and the AppImage. Also
  removed: the MXE cross-build script for Windows (Qt 5, no OCR) and upstream's second Linux
  workflow (`cmake.yml`), which built the packages once more without tests.
* Fixed compiler warnings: a possibly dangling reference in "Go to Page", the member
  initialization order in the margins options, and an obsolete combo box setting that Qt 6
  ignored anyway.
* The `update_translations` target works with Qt 6 again: it passes a file list to `lupdate`
  instead of a qmake project file, which Qt 6's `lupdate` no longer reads, and includes the
  sources of the application itself (main window and dialogs) again.
* A script creates the portable Windows ZIP (`scripts\package-windows.ps1`, see below).
* The release workflow builds the portable Windows ZIP as well, with the same script, and
  attaches it to the release together with the Linux packages. The shipped OCR languages
  (German and English from tessdata_best) are fixed to one commit and checked against their
  SHA-256. Started by hand ("Run workflow"), it builds all packages as a test, without
  creating a release.
* The `update_translations` target no longer refers to `Qt6::lupdate` by name, which broke the
  Qt 5 fallback build. A review of the Qt 6 port found no other problem: the code already
  guards every API removed in Qt 6.

## Building

### Windows, step by step

These steps assume a machine with nothing installed yet. Neither JOM nor vcpkg comes with an
installer: they are simply unpacked into a folder of your choice. The examples below use
`C:\Dev` as that folder – any path works, as long as you use it consistently and it contains
no spaces or non-English characters.

**1. Get the source code** into `C:\Dev\scantailor-ocr`, either with
[Git for Windows](https://git-scm.com/download/win):

```
cd /d C:\Dev
git clone https://github.com/2ndmax/scantailor-ocr.git
```

or by downloading the ZIP of this repository ("Code" → "Download ZIP") and unpacking it there.

**2. Install Visual Studio Community** (the free edition):
<https://visualstudio.microsoft.com/vs/community/>
In the installer pick the workload **"Desktop development with C++"**. Of its optional
components, only these are needed:

* MSVC build tools (x64)
* C++ CMake tools for Windows
* Windows 11 SDK

**3. Download JOM** (a parallel `nmake` replacement): <https://wiki.qt.io/Jom>
Unpack it to `C:\Dev\jom`, so that `C:\Dev\jom\jom.exe` exists.

**4. Download vcpkg** as a ZIP: <https://github.com/microsoft/vcpkg>
Unpack it to `C:\Dev\vcpkg`.

**5. Build the libraries with vcpkg.** Open the **"Native Tools Command Prompt for VS x64"**
(from the start menu, inside the Visual Studio folder) and run:

```
cd /d C:\Dev\vcpkg
bootstrap-vcpkg.bat
vcpkg install --recurse qtbase qtsvg qttools qttranslations libjpeg-turbo libpng "tiff[core,jpeg,zip,lzma,zstd,webp,lerc,libdeflate,tools]" openjpeg zlib boost-test boost-foreach boost-intrusive boost-multi-index boost-lambda tesseract
```

`tesseract` is only needed for text recognition in the PDF export (see the build options
below).

This compiles Qt and everything else from source and takes a while – plan for an hour or more
and several gigabytes of disk space.

**6. Set the environment variables.** Open **PowerShell as administrator** and run (adjust the
two paths if you used a different folder):

```powershell
$vcpkgRoot = "C:\Dev\vcpkg"
$jomRoot   = "C:\Dev\jom"
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "Machine")
[System.Environment]::SetEnvironmentVariable("JOM_ROOT",   $jomRoot,   "Machine")
$currentPath = [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
[System.Environment]::SetEnvironmentVariable("PATH", "$currentPath;$vcpkgRoot;$jomRoot", "Machine")
```

Then **restart the computer**, so every program sees the new variables.

**7. Configure and build.** In the "Native Tools Command Prompt for VS x64":

```
cd /d C:\Dev\vcpkg
vcpkg integrate install

cd /d C:\Dev\scantailor-ocr
mkdir build
cd build
cmake -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ..
jom -j 10
```

`-j 10` is the number of parallel compiler processes; use roughly the number of processor cores.
The finished `scantailor-ocr.exe` and all needed DLLs end up in the `build` directory.
For text recognition, download languages in the program (step "Create PDF" → "More languages ..."),
or put the language files into `build\tessdata` by hand, e.g. for German and English
(about 24 MB together):

```
mkdir tessdata
curl -L -o tessdata\deu.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/deu.traineddata
curl -L -o tessdata\eng.traineddata https://github.com/tesseract-ocr/tessdata_best/raw/main/eng.traineddata
```

**8. Run the tests** (optional), in the same `build` directory:

```
ctest -C Release --output-on-failure
```

**9. Create the portable ZIP** (optional), from the repository folder:

```
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -Version 1.0.8
```

The script collects the program, exactly the DLLs it needs (read from the import tables, so
test libraries are left out), the Qt plugins, the translations, the OCR languages German and
English from `build\tessdata` and the license texts of all components (from vcpkg) into
`build\package`. It then starts a copy with a minimal `PATH` for a few seconds to check that
nothing is missing, and packs the ZIP. `-Languages` selects other languages to ship,
`-SkipStartTest` skips the start test; see the comments at the top of the script.
The ZIPs on the releases page are built the same way by GitHub Actions.

When configuring again later, e.g. after changing build options, add `--fresh` to the `cmake`
call to discard the cached configuration:

```
cmake --fresh -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ..
```

### Linux (Debian / Ubuntu)

Qt 6.4 or later is required (Ubuntu 24.04 or later):

```
sudo apt install build-essential cmake \
  qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6opengl6-dev libgl-dev \
  libboost-test-dev libboost-dev libjpeg-dev libpng-dev libtiff-dev zlib1g-dev libopenjp2-7-dev \
  libtesseract-dev libleptonica-dev tesseract-ocr-deu tesseract-ocr-eng qt6-translations-l10n
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Build options

* `-DTIFF_REQUIRED_CODECS=...` / `-DTIFF_RECOMMENDED_CODECS=...` – the TIFF compression schemes
  libtiff has to / should support (defaults: `LZW;PACKBITS;CCITT;JPEG;OJPEG;DEFLATE;LZMA` /
  `ZSTD;WEBP;LERC`). `-DSKIP_TIFF_CODEC_CHECK=ON` skips the check.
* `-DENABLE_CODE_ANALYSIS=ON` – extra warnings and static code analysis. Slow; best used in a
  separate build directory.
* `-DBUILD_TESTS=OFF` – don't build the unit tests.
* `-DENABLE_OCR=OFF` – build without text recognition, so Tesseract isn't needed. The PDF export
  still works, without a text layer. With the default `ON`, configuring fails if Tesseract
  (4.1 or newer) isn't found.

## License

GNU GPLv3, see [LICENSE](LICENSE). This is a modified version of ScanTailor Advanced;
the modifications are documented above and in the commit history.
