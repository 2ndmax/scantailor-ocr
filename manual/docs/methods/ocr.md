# Text Recognition

ScanTailor OCR recognizes text with [Tesseract](https://github.com/tesseract-ocr/tesseract),
the open source OCR engine. The recognized text is used for:

- the invisible **text layer** of the PDF, which makes it searchable and lets you select
  and copy text,
- the text formats **TXT**, **hOCR** and **ALTO**.

The page images stay unchanged. The text layer lies invisibly on top of them, word by
word in the right place.

## How Tesseract reads a page

1. **Layout analysis** finds blocks of text, columns, pictures and lines.
2. Each line is read by a neural network (LSTM) trained for one language or script. It
   reads whole lines, not single letters, and uses what it learned about the language
   to decide between similar shapes.
3. The words, their positions and their confidence are passed back.

The **Text layout** setting changes step 1:

| Text layout | What Tesseract does |
|---|---|
| **Automatic** | Detects columns, pictures and captions itself. |
| **Single column** | Reads the page as one column of text. Fixes the reading order of indented lines and tables. |
| **Single block of text** | Reads the page as one block. For labels and notes. |

## Languages

The language model matters: German needs `deu` to read ä, ö, ü and ß correctly, a book
in Fraktur needs a Fraktur model. Tick all languages that occur on the pages, but no
more than necessary.

The languages come from the
[tessdata_best](https://github.com/tesseract-ocr/tessdata_best) collection, the most
accurate models of the Tesseract project. English and German come with the program;
**More languages ...** in the Export step downloads others. The collection also has
**script** models, which cover all languages written in one script (for example
Latin, Cyrillic or Fraktur).

Language files are looked for:

- next to the program,
- in the user's application data folder (downloads go here),
- in the folder named by the environment variable `TESSDATA_PREFIX`,
- on Linux, in the folders of the system packages.

You can also copy `*.traineddata` files there by hand, for example on a computer without
internet.

## Getting good recognition

- **Black and white** pages give the best results; the cleaner the page, the better the
  text.
- **Resolution:** letters should be at least about 20 pixels high. 300 DPI is enough for
  normal print, small print needs more. An output resolution of 600 DPI is a good choice.
- Pages with **split output** are recognized on their black and white text layer.
- Straight lines help: use [Deskew](../steps/deskew.md) and, for curved pages,
  [Dewarping](dewarping.md).

## The text formats

| Format | Content | Use |
|---|---|---|
| **TXT** | Plain text of all pages, pages separated by a form feed. | Reading, search, further processing |
| **hOCR** | One HTML file with every word and its position. | Programs that process recognized text |
| **ALTO** | XML, one file per page, named by its place in the export and its image (`0001_scan_017.xml`). | Libraries, archives, digitization projects |

The text is Tesseract's, unchanged. hOCR and ALTO name the output file of each page as
its image.
