# Compression

Compression happens twice, in two separate places:

1. **TIFF files in the output folder**, written by the *Output* step
   (*Output > TIFF Compression*).
2. **The PDF**, written by the *Export* step (*Export > PDF Compression*).

The PDF is built from the TIFF files, so losses in the TIFF files can't be undone in the
PDF. Both settings apply to the whole project and have one part for each kind of page.

## Lossless and lossy

- **Lossless** methods store every pixel exactly. The file is smaller, but decoding gives
  back the very same image.
- **Lossy** methods (JPEG, and JPEG 2000 below quality 100) throw away detail the eye is
  unlikely to miss. Files become much smaller, but some detail is gone for good, mostly
  at sharp edges such as the edges of letters.

## Black and white pages

| Method | Where | Size | Notes |
|---|---|---|---|
| **JBIG2** | PDF | Smallest | Lossless; about a third smaller than CCITT G4. The default for the PDF. |
| **CCITT G4** | TIFF, PDF | Small | Lossless; the fax standard, shown by even very old PDF programs. |
| **LZW**, **Deflate**, **LZMA** | TIFF | Larger | Lossless general-purpose methods. |
| **Deflate**, **None** | PDF | Much larger | Lossless. |

!!! info "JBIG2 without the risk"
    JBIG2 has a "symbol mode" that reuses similar-looking letters and can, in rare cases,
    swap one character for another (e.g. a 6 for an 8). ScanTailor OCR never uses it:
    it only uses the lossless generic coding, so every pixel stays exactly as it is.

## Color and grayscale pages

| Method | Where | Lossless | Notes |
|---|---|---|---|
| **JPEG** | TIFF, PDF | No | Small files, slight losses mostly at text edges. Fast. Shown everywhere. The default for the PDF. |
| **JPEG 2000** | PDF | At quality 100 | Better quality than JPEG at the same size, or smaller files at the same quality. Much slower to create; some simple or old PDF programs can't show it. |
| **LZW**, **Deflate** | TIFF | Yes | Deflate is usually smaller than LZW. |
| **LZMA** | TIFF | Yes | The smallest lossless TIFF files, but slow, and few other programs can read them. ScanTailor OCR and its export can. |
| **Deflate** | PDF | Yes | Very large: a color page can take 10 to 25 MB. |
| **None** | TIFF, PDF | Yes | Up to 45 MB for a color page in the PDF. |

**Quality** (JPEG, JPEG 2000): higher values give better pictures and larger files. The
JPEG 2000 value is meant to look like the same JPEG value; 100 is lossless.

## Posterized pages

Pages whose colors were reduced to a few (see
[Reducing colors](color-modes.md#reducing-colors)) are stored best with **Deflate**: it
is lossless and keeps the few colors exactly, which makes small, sharp files. JPEG and
JPEG 2000 would convert the page to full color first, which blurs the color areas and
often makes the file larger.

## Mixed pages

Without split output, a mixed page is one color image, and the PDF stores it as one:
with JPEG the text gets blurred, with a lossless method the file gets very large.

With **Split output** (in *Output > Splitting*), the PDF stores two layers:

- the **text** as a lossless JBIG2 black and white mask, at full resolution, on top;
- only the **picture area** of the background as JPEG underneath, at full, half or one
  third of the output resolution (*Background layer*).

The text stays perfectly sharp, and the page is usually much smaller.

## TIFF files in the output folder

The TIFF files are the working copies from which the PDF is made. Lossless methods are
the safe choice: the export can then decide about losses once, in the PDF.

!!! warning "JPEG in the TIFF files"
    JPEG compression of the TIFF files makes the output folder much smaller, but the PDF
    then compresses the pictures a second time, and the losses add up. On mixed pages
    without split output the text gets blurred. The [Quality](../steps/export.md#quality)
    panel of the export warns about this.

## Recommended settings

| Goal | TIFF (Output) | PDF (Export) |
|---|---|---|
| **Small PDF for reading** (the defaults) | lossless | JBIG2, JPEG at 85, background layer half |
| **High quality** | lossless | JBIG2, JPEG 2000 at 90 or more |
| **Archive, lossless** | lossless | JBIG2, JPEG 2000 at 100 or Deflate, PDF/A-2b |
| **Very old PDF readers** | lossless | CCITT G4, JPEG |
