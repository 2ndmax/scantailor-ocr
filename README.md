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

## What's different from ScanTailor Advanced

Besides the PDF export and text recognition, among other things:

* reworked oblique correction in *Deskew* and evened-out letter widths after dewarping
* automatic import of new scans from a watched folder
* more output options: split output with the original background, grayscale output, TIFF
  compression, removal of color noise
* one consistent layout and wording for the options panels of all steps
* features adopted from ScanTailor Experimental and ScanTailor Spectre
* the program in English and German

All changes are listed in [CHANGES.md](CHANGES.md).

## Download

Ready-to-run versions for Windows (portable ZIP) and Linux (`.deb`) are on the
[releases page](https://github.com/2ndmax/scantailor-ocr/releases).

## Building

See [BUILDING.md](BUILDING.md) for building on Windows and Linux, the build options and the
optional code analysis tools.

## License

GNU GPLv3, see [LICENSE](LICENSE). This is a modified version of ScanTailor Advanced;
the modifications are documented in [CHANGES.md](CHANGES.md) and in the commit history.

The changes were developed with the help of Claude (Anthropic).
