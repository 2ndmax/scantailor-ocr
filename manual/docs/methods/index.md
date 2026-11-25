# Methods Explained

The step pages say *what* a setting does. These pages explain *how* the methods behind
them work and *when* to use which, with examples.

| Page | Explains |
|---|---|
| [Color Modes](color-modes.md) | Black and white, color / grayscale and mixed; automatic color mode detection; reducing colors |
| [Binarization](binarization.md) | All 15 methods of turning gray into black and white, compared on one page |
| [Enhancement Filters](enhancement.md) | Removing color noise, evening out the background, denoising and sharpening |
| [Dewarping](dewarping.md) | Straightening bent lines and trapezoid distortion |
| [Compression](compression.md) | TIFF and PDF compression, lossless and lossy, layers of mixed pages |
| [Text Recognition](ocr.md) | How Tesseract reads a page, languages, text formats |

## The processing chain

In the *Output* step, a page passes through these stages, in this order:

```
scan (gray or color), deskewed and cropped, dewarped if turned on
 → equalize illumination
 → enhancement filters          (color noise, background, denoisers, sharpen)
 → "Colored", Savitzky-Golay smoothing
 → binarization                 (black and white and mixed pages)
 → morphological smoothing, despeckling
 → picture zones, fill zones, photo adjustments
 → TIFF file in the output folder
```

The export then reads the TIFF files, recognizes the text and writes the PDF.
