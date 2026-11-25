# Color Modes

Each page is stored in one of three color modes, chosen in *Output > Mode*. The choice
decides the size of the PDF more than any other setting.

| | Black and White | Color / Grayscale | Mixed |
|---|---|---|---|
| **Stores** | 1 bit per pixel | The whole page as an image | Text in black and white, pictures in color or gray |
| **Text** | Sharpest | As scanned | Sharpest |
| **Pictures** | Lost (become dots or blotches) | Kept | Kept |
| **Paper tone** | White | Kept | White (or original with *Original background*) |
| **Size of a page in the PDF** | Tiny | Large | Small, with split output |
| **For** | Text, line drawings, formulas | Photos, art, maps, color plates | Text with photos or illustrations |

## Black and White

The page becomes pure black and white, using one of the
[binarization methods](binarization.md). Text pages are best in this mode: the letters
are sharp, the paper is clean white, the file is very small, and text recognition works
best on it.

## Color / Grayscale

The page is stored as it looks, in color or, with **Grayscale output**, in gray. Use it
for pages where the image itself matters. The [enhancement filters](enhancement.md) and
the [photo adjustments](../steps/output.md#photo-adjustments) improve such pages.

## Mixed

The page is divided into **picture zones**, which keep their color or gray values, and
the rest, which becomes black and white. This gives sharp text and real pictures on the
same page.

The picture zones are found automatically (as a *Contour mask* or as *Rectangles*) and
can be corrected on the *Picture Zones* tab. See
[Picture Zones](../steps/output.md#picture-zones).

!!! tip "Turn on split output for mixed pages"
    With **Split output** (in *Output > Splitting*), the PDF stores a mixed page as two
    layers: the text as a lossless black and white mask, the pictures as a small JPEG
    underneath. Without it, the whole page is stored as one color image, which makes the
    PDF much larger and the text less sharp. The [Export](../steps/export.md#quality)
    step warns about mixed pages without split output.

## Detecting the color mode automatically

In a book with a few photos among hundreds of text pages, setting each page by hand is
tedious. **Color Mode Detection > Detect ...** looks at the chosen pages and sets their
mode:

- A page with color **outside pictures** above the **Color from** share becomes
  *Color / Grayscale*. Smaller colored parts (headings, boxes, thumb index tabs) are
  ignored.
- A page whose color is **only in pictures** becomes *Mixed*.
- With **Also look for grayscale pictures**, black and white pages whose largest picture
  covers at least the **Pictures from** share also become *Mixed*. This finds gray
  photos, but takes longer.
- All other pages become *Black and White*.

The detection doesn't create output files, so it is quick. Sort the thumbnails by color
mode afterwards to check the result.

## Light text on dark background

Pages with white text on black (or other light-on-dark content) are detected during
*Deskew* and inverted internally, so the detection of skew and content works. In
*Output*, **Dark content on a light background** is then turned off for them. The
detection can be turned off in the [Default Parameters](../reference/default-parameters.md).

## Reducing colors

*Color Operations > Reduce colors* limits the number of colors:

- **Posterize** divides each color channel into a fixed number of levels (*Level* 4
  gives at most 4 × 4 × 4 colors, usually far fewer). *Normalize* spreads the colors
  between black and the paper first, for pale scans. *Force black and white* makes dark
  and light gray gradients black and white.
- **K-means** finds the given number of **Colors** that fit the page best, measured in a
  color space close to human perception (CIE Lab). With **Fixed black**, one of them is
  pure black, so black text stays black instead of getting a tint.

On black and white and mixed pages, this applies to the colors of the text and needs
*Color segmentation*. On color pages, *Posterize* reduces the whole page. Posterized
pages are stored losslessly and compactly in the PDF with Deflate.
