# ScanTailor OCR – scanned pages to searchable PDF

ScanTailor OCR cleans up scanned pages and turns them into a **compact, searchable PDF**:

* **PDF export** built in, directly from the project's output: lossless JBIG2 for black and
  white pages, layered pages (text over a small JPEG picture) for mixed pages, optionally as
  **PDF/A-2b** for long-term archiving.
* **Text recognition (OCR)** with Tesseract adds an invisible text layer, so the PDF can be
  searched and its text copied. More languages can be downloaded in the program.
* **Text formats** TXT, hOCR and ALTO for further processing, also without a PDF.
* A **command line tool**, `scantailor-ocr-cli`, processes a project and exports it without
  opening a window.

Website: [www.ScanTailorOCR.eu](https://www.ScanTailorOCR.eu)

## What's different from ScanTailor Advanced

Besides the export, the text recognition and the command line tool, among other things:

* a new automatic oblique (shear) correction in *Deskew*, which takes the angle from the
  straight text edges, and evened-out letter widths after dewarping
* new despeckling, which keeps dots over letters, umlauts and punctuation far more often
* automatic import of new scans from a watched folder
* more output options: split output with the original background, grayscale output, TIFF
  compression, removal of color noise
* the settings for the whole project (compression, text recognition, color mode detection)
  are stored in the project, so it gives the same result on another computer
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

## Using the program

There is no manual of its own yet. The basic way of working is still that of ScanTailor: a
project goes through the steps one after another, page by page or as a batch. The
[documentation of ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced#readme)
can help to get started, but it is old, and much has changed since: panels were merged and
renamed, options added or removed, and some steps work differently. Where it no longer matches
the program, go by the program and its tooltips. [CHANGES.md](CHANGES.md) lists every change.

## Project files

ScanTailor OCR saves projects in a new format (project file version 10). Because of the many
changes, this format is not backward compatible: ScanTailor Advanced can't open projects saved
by ScanTailor OCR.

Projects of ScanTailor Advanced (version 3 of 1.1, version 4 of 1.2) can usually be opened. Some
of their settings may be processed differently, though, so the results can differ from those of
ScanTailor Advanced. Settings that such a project doesn't contain are taken from the active
profile of the default parameters. Once the project is saved in ScanTailor OCR, also
automatically, it has the new format and can no longer be opened in ScanTailor Advanced.
ScanTailor OCR tells this when opening the project and offers to make a backup copy first
(`Book_BACKUP.ScanTailor` next to `Book.ScanTailor`), which stays usable in ScanTailor Advanced.

## How this project came about

ScanTailor OCR started from
[ScanTailor Advanced](https://github.com/ScanTailor-Advanced/scantailor-advanced) 1.2.1, an
interactive post-processing tool for scanned pages (page splitting, deskewing, content
selection, margins, dewarping and more).

All changes made since were developed entirely with the help of
[Claude Code](https://claude.com/claude-code), an AI coding assistant: the new features,
adoptions and fixes, the translations, the documentation and the website were planned, built
and tested in this way.

This would not have been possible without the work of others. Sincere thanks go to everyone
who contributed to ScanTailor and its forks, from the original ScanTailor to ScanTailor
Advanced, ScanTailor Experimental and ScanTailor Spectre. They came up with new features,
developed them and built them into their projects with a lot of effort and dedication, and
ScanTailor OCR builds on all of it.

## Building

See [BUILDING.md](BUILDING.md) for building on Windows and Linux and the build options,
[TESTING.md](TESTING.md) for the tests and developer tools, and
[scripts/README.md](scripts/README.md) for the setup and code analysis scripts.

## License

GNU GPLv3, see [LICENSE](LICENSE). This is a modified version of ScanTailor Advanced;
the modifications are documented in [CHANGES.md](CHANGES.md) and in the commit history.

Some parts are licensed under the Apache License 2.0: the JBIG2 coder taken from
[jbig2enc](https://github.com/agl/jbig2enc) (see [src/core/jbig2enc](src/core/jbig2enc)) and
Tesseract's GlyphLessFont and the head of the hOCR and ALTO files. The release packages include these license texts and those of the
libraries they bundle, such as Qt (GNU LGPL v3) and Tesseract: the Windows ZIP in its `licenses`
folder, the .deb and the AppImage under `share/doc`.
