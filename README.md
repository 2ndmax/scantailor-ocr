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
    Deflate or None, the quality and the resolution of the pictures of split pages.
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
* *Deskew*: the panel **Deskew and Oblique** has the headings "Deskew" and "Oblique". The options
  of its **Apply to ...** dialog are named like them, *Deskew* and *Oblique* (formerly "Deskew
  angle and mode" and "Oblique angle and mode").
* *Select Content*: "Page Box" and "Content Box" are one panel, **Page and Content Box**.
* *Page Layout*: the button "Match size to all pages" is gone. It applied the whole alignment
  (not only the matched size) to all pages, like **Apply to ...** with all pages, and didn't
  store the check box "Match size with other pages" for the current page. The tooltip of
  **Apply to ...** says what is applied. The nine alignment buttons are centered, also in the
  Default Parameters window.
* *Output*: the panel **Mode** has the headings "General" (formerly "Options"), "Denoising",
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
* *Margins*: "Lock aggregate size for matching" applies to all pages, not to the page, so it
  has its own panel, **Common Size**, below **Alignment** (no **Apply to ...**). The panel
  shows the common size of the pages whose size is matched, in the current units; the check box
  is called "Lock for now", and its tooltip says that the locked size also applies to the
  output and is released when the project is closed. Releasing the lock now updates the
  thumbnails.
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
* *Output*: **Wiener denoiser** has its own heading, "Denoising", right after "General" (also in
  the default parameters dialog), instead of being under "Color Operations": it reduces the
  noise of the image before all other processing, in every color mode.
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
  read with or without it). The color mode list has its own bold heading, **Color Mode**.
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
* *Output*, **Picture Zones**: the shapes are called "Free shape" and "Rectangle" (formerly "Free"
  and "Rectangular").
* "Folder" instead of "directory", as in Windows, e.g. **Input Folder** and **Output Folder** in
  the "Project Files" dialog.
* "Project Files" dialog: after moving files to the other list, the next file is selected, so
  the arrow button can be clicked repeatedly without selecting a file each time.
* German translation: the same thing is called the same everywhere (e.g. "Schwarz-Weiß",
  "Geraderichten", "Ordner"), and "..." is preceded by a space.
* *Output*, Wolf threshold: "Lower bound:", "Upper bound:" and "Coef:" as with Sauvola (formerly
  "Upper Bound:" and "Coeff:").
* Check boxes in sentence case also in *Select Content* and *Margins*: "Fine tune page corners"
  and "Auto margins". "Resolution:" has a colon like the other labels, "Color:" (*Output*,
  Filling) no longer a trailing space. *Deskew*: the detection method is called "Top page edge";
  that it is meant for book scans with a dark background stays in the tooltip.

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
* The window opens large enough to show the largest tab without scrolling, at most as large as
  the screen. It can be made lower, but not narrower, so only the height ever scrolls.
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

### Start page

* "New Project" and "Open Project" are buttons now instead of blue links.
* Recent projects are listed with their name in the normal text colour and their folder
  below in smaller, slightly faded text; an entry is highlighted in grey under the mouse and
  opens on click or with Enter/Space. The full path is shown as a tooltip.
* The start page has the window background, so its text is no longer drawn on the mid-grey
  image area of the light colour scheme.

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
* Code cleanups based on the compiler's static code analysis.

### Build and tests

* CMake verifies which compression schemes libtiff supports (LZMA etc. are required).
* Optional static code analysis: `-DENABLE_CODE_ANALYSIS=ON` (MSVC: `/W4 /analyze`).
* New tests for the image readers and the TIFF writer; CI now fails on failing tests and also
  builds on Windows.
* New tests for the PDF export: file structure and cross-reference offsets, page size from the
  resolution, lossless G4 and JBIG2 round trips (JBIG2 with a decoder written from the standard
  for the tests, and on Linux additionally rendered by Poppler's `pdftoppm` and compared pixel
  by pixel), cropping of split backgrounds, page order, text layer,
  finding and gathering language files, download checksums.
* CI installs Tesseract on Linux and Windows; the Flatpak is built without text recognition for
  now, as its runtime doesn't include Tesseract.
* CI builds Linux with both Qt 5 and Qt 6; the Linux packages (DEB, RPM, AppImage) are built
  with Qt 6, like the Windows version. The Windows job keeps vcpkg's intermediate files off the
  runner's small system drive and only saves a new package cache when something was rebuilt.
* The Lint workflow uses clang-format 22.1.3, the version that comes with Visual Studio 2026, so
  that a local check gives the same result (other versions format some lines differently).
* The Flatpak manifest (now `flatpak/io.github._2ndmax.ScanTailorOCR.json`) builds this fork's own code
  instead of upstream's version 1.1.1, with the KDE 6.10 runtime (Qt 6), Boost as an extra
  module and the correct program name. The Flatpak workflow can be started by hand.
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
powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -Version 1.0.7
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

With Qt 6 (recommended; the Linux packages from CI are built this way):

```
sudo apt install build-essential cmake \
  qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6opengl6-dev libgl-dev \
  libboost-test-dev libboost-dev libjpeg-dev libpng-dev libtiff-dev zlib1g-dev libopenjp2-7-dev \
  libtesseract-dev libleptonica-dev tesseract-ocr-deu tesseract-ocr-eng qt6-translations-l10n
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Qt 5 still works: install `qtbase5-dev libqt5svg5-dev qttools5-dev qttools5-dev-tools
libqt5opengl5-dev` instead of the Qt 6 packages. If both are installed, CMake picks Qt 6;
add `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON` to the `cmake` call to use Qt 5.

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
