# The Steps

A project goes through seven steps. Each step works on the result of the one before.

| Step | Job | Typical work for you |
|---|---|---|
| [1 Import](import.md) | Rotate the scans upright, trim scanner edges, import new scans | Rotate once, apply to all |
| [2 Split Pages](split-pages.md) | Split two-page scans into single pages | Check the split line |
| [3 Deskew](deskew.md) | Straighten tilted and sheared pages | Usually none |
| [4 Select Content](select-content.md) | Find the page and the text on it | Fix boxes that miss text or catch dirt |
| [5 Margins](margins.md) | Margins, common page size, alignment | Set margins once |
| [6 Output](output.md) | Black and white, cleanup, dewarping, pictures | Choose the color mode, check the result |
| [7 Export](export.md) | PDF with text layer, TXT, hOCR, ALTO | Tick languages, click Export |

## Image steps and page steps

Steps 1 and 2 work on the **scanned images**. From step 2 on, a two-page scan becomes two
pages, and steps 3 to 7 work on the **pages**. That's why the number of thumbnails can
change between step 1 and step 3.

## Automatic and manual

Most settings have an **Auto** and a **Manual** button:

- **Auto**: ScanTailor OCR detects the value on each page by itself.
- **Manual**: the value you set stays, also when the page is processed again.

Dragging a line or a handle in the page view switches that setting to *Manual*
automatically. To let the program detect it again, click *Auto*.

## Keyboard shortcuts for the steps

| Key | Step |
|---|---|
| ++i++ | 1 Import |
| ++e++ | 2 Split Pages |
| ++n++ | 3 Deskew |
| ++v++ | 4 Select Content |
| ++k++ | 5 Margins |
| ++p++ | 6 Output |

More in [Keyboard and Mouse](../reference/keyboard.md).
