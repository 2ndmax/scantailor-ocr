# ScanTailor OCR – scanned pages to searchable PDF

ScanTailor OCR cleans up scanned pages and turns them into a **compact, searchable PDF**:

* **PDF export** built in, directly from the project's output: lossless JBIG2 for black and
  white pages, layered pages (text over a small JPEG picture) for mixed pages.
* **Text recognition (OCR)** with Tesseract adds an invisible text layer, so the PDF can be
  searched and its text copied. More languages can be downloaded in the program.

Website: [www.ScanTailorOCR.eu](https://www.ScanTailorOCR.eu)

**For the page processing features and how to use them, please see the
[documentation of ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced#readme).**
A lot of what is described there applies to ScanTailor OCR as well.

## How this project came about

ScanTailor OCR is based on [ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced)
1.2.1, an interactive post-processing tool for scanned pages (page splitting, deskewing, content
selection, margins, dewarping and more).

All changes made on top of it were developed entirely with the help of
[Claude Code](https://claude.com/claude-code), an AI coding assistant: the new features,
adoptions and fixes, the translations, the documentation and the website were planned, built
and tested in this way.

This would not have been possible without the work of others. Sincere thanks go to everyone
who contributed to ScanTailor and its forks, from the original ScanTailor to ScanTailor
Advanced, ScanTailor Experimental and ScanTailor Spectre. They came up with new features,
developed them and built them into their projects with a lot of effort and dedication, and
ScanTailor OCR builds on all of it.

## What's different from ScanTailor Advanced

Besides the PDF export and text recognition, among other things:

* reworked oblique correction in *Deskew* and evened-out letter widths after dewarping
* automatic import of new scans from a watched folder
* more output options: split output with the original background, grayscale output, TIFF
  compression, removal of color noise
* one consistent layout and wording for the options panels of all steps
* the program in English and German

All changes are listed in [CHANGES.md](CHANGES.md).

### Adopted from ScanTailor Experimental

[ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental)
is a separate fork of the original ScanTailor. Its code is structured differently, so these
features were reimplemented:

* scaling pages to a common output size in *Margins*
* the filters "Even out background", "KNN denoiser" and "Sharpen" in *Output*
* five more binarization methods, and settings for every method
* K-means for colored text
* double-click to show the whole page again after zooming in

### Adopted from ScanTailor Spectre

[ScanTailor Spectre](https://github.com/abandoned-industries/scantailor-spectre) is a macOS fork
of ScanTailor Advanced. Taken over from it:

* split line along the spine in *Split Pages*
* settings for the automatic content detection in *Select Content*, for art books and photo pages
* automatic detection of the color mode in *Output*
* photo adjustments in *Output* (exposure, contrast, highlights, shadows, whites and blacks)

The details, including what was deliberately not adopted, are in [CHANGES.md](CHANGES.md).

## Download

Ready-to-run versions for Windows (portable ZIP) and Linux (`.deb` and AppImage) are on the
[releases page](https://github.com/2ndmax/scantailor-ocr/releases).

## Building

See [BUILDING.md](BUILDING.md) for building on Windows and Linux, the build options and the
optional code analysis tools.

## License

GNU GPLv3, see [LICENSE](LICENSE). This is a modified version of ScanTailor Advanced;
the modifications are documented in [CHANGES.md](CHANGES.md) and in the commit history.

The changes were developed with the help of Claude (Anthropic).
