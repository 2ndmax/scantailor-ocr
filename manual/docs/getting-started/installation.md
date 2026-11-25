# Installation

Ready-to-run versions are on the
[releases page](https://github.com/2ndmax/scantailor-ocr/releases). All of them include
text recognition (Tesseract) and the languages English and German.

=== "Windows"

    1. Download the **ZIP file** for Windows.
    2. Unpack it into a folder of your choice, for example `C:\Programs\ScanTailor OCR`.
    3. Start `scantailor-ocr.exe`.

    The program is portable: it needs no installation. The command line tool
    `scantailor-ocr-cli.exe` is in the same folder.

=== "Linux"

    Two packages are available:

    - **.deb** for Debian, Ubuntu and their relatives. Install it with your package
      manager or with `sudo apt install ./scantailor-ocr-<version>.deb`.
    - **AppImage** for most other distributions. Make it executable
      (`chmod +x scantailor-ocr-<version>-x86_64.AppImage`) and start it.

    The command line tool is called `scantailor-ocr-cli`.

## First start

On the first start, the program picks English or German from the language of your
system. You can change it later under *Settings > Program Settings* (see
[Program Settings](../reference/program-settings.md)).

If ScanTailor Advanced was installed before, ScanTailor OCR copies its settings, saved
profiles and OCR languages once. The old files are kept.

## More OCR languages

English and German come with the program. More languages can be downloaded inside the
program, in the [Export](../steps/export.md#text-recognition-ocr) step with
**More languages ...**.

## Using it next to ScanTailor Advanced

ScanTailor OCR can be installed next to ScanTailor Advanced. It opens projects of
ScanTailor Advanced, but once it has saved them, ScanTailor Advanced can no longer open
them. See [Projects from ScanTailor Advanced](projects.md#projects-from-scantailor-advanced).
