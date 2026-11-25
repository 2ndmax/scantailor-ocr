# Tips & FAQ

## Scanning

??? question "Which resolution should I scan at?"
    300 DPI is enough for normal print. Use 400 to 600 DPI for small print, footnotes,
    formulas and old typefaces. Higher than 600 DPI rarely helps and makes everything
    slower. Set the **output** resolution to 600 DPI for black and white pages; that
    gives smooth letters also from 300 DPI scans.

??? question "Color, grayscale or black and white scans?"
    Scan in **grayscale**, or in color if the book has color. Don't let the scanner make
    black and white: ScanTailor OCR does it far better from gray values, and it can
    correct shadows and stains before it does.

??? question "Can I scan and process at the same time?"
    Yes. Turn on **Watch folder** in step *1 Import* and let your scanning program save
    into that folder. See
    [Automatic import](getting-started/projects.md#automatic-import-of-new-scans).

## Results

??? question "Text near the spine is black or blotchy."
    The shadow of the spine fell below the threshold. Make sure **Equalize illumination**
    is on, or try a local binarization method such as **Wolf**, **Gatos** or **Robust**.
    See [Binarization](methods/binarization.md).

??? question "The letters are too thin and break up."
    Raise **Delta** a little (positive values make letters thicker), or choose a method
    that gives bolder letters, such as **Wolf**, **Window** or **Nick**.

??? question "The letters are too bold; e, a and o fill up."
    Lower **Delta** (negative values), or choose a method with thinner strokes, such as
    **Sauvola** or **Singh**. A higher output resolution also helps.

??? question "Dots, commas or accents are missing."
    Lower the **Despeckle** slider in *Output*. The *Despeckling* tab shows what is being
    removed.

??? question "A page number or a line at the edge is cut off."
    The content box in *Select Content* is too small. Drag it larger, double-click the
    missing text, or tick **Ignore page box during detection**. If many pages are
    affected, raise **Border tolerance**.

??? question "Dirt or the edge of the facing page shows up in the output."
    Make the content box in *Select Content* smaller, and make sure **Fill margins** and
    **Fill offcut** are on in *Output*. For a single stain inside the text, draw a fill
    zone on the *Fill Zones* tab.

??? question "Photos come out as black blotches."
    The page is in *Black and White* mode. Set it to **Mixed**, or let
    **Detect ...** under *Color Mode Detection* find such pages.

??? question "Lines near the spine are bent."
    Turn on **Dewarp** in *Output* with the method *Auto*. See
    [Dewarping](methods/dewarping.md).

??? question "Output says the final size of pages is not yet known."
    Run the batch processing (▶) in *Select Content* or *Margins* first. The common page
    size is only known when all pages have been through these steps.

## PDF

??? question "My PDF is very large."
    Check the [Quality](steps/export.md#quality) panel of the export. Typical causes:
    mixed pages without **Split output**, color pages that could be black and white, and
    Deflate or None as PDF compression for color pages. For color pages, JPEG with
    *Background layer* at half resolution keeps files small.

??? question "The text I copy from the PDF comes out in the wrong order."
    Set **Text layout** to **Single column** in the export and export again.

??? question "Text recognition is poor."
    Tick the right **language** (Fraktur needs its own model), make sure the lines are
    straight, and export from black and white pages at 300 to 600 DPI. See
    [Text Recognition](methods/ocr.md#getting-good-recognition).

??? question "Adobe Acrobat won't let me edit the PDF."
    PDF/A files open read-only in Acrobat at first; editing has to be enabled there.
    Untick **PDF/A-2b** if you don't need an archival PDF.

## Projects

??? question "I moved my project and now images are missing."
    Use *Tools > Relink ...* to tell the project where the images and the output folder
    are now. See [Relink](reference/tools.md#relink).

??? question "Can I open my ScanTailor Advanced projects?"
    Yes. ScanTailor OCR offers a backup copy first, because ScanTailor Advanced can't
    open the project anymore once ScanTailor OCR has saved it. See
    [Projects from ScanTailor Advanced](getting-started/projects.md#projects-from-scantailor-advanced).

??? question "How do I use the same settings for every book?"
    Set them up in *Settings > Default Parameters* and save them as a profile. New
    projects start with the active profile. See
    [Default Parameters](reference/default-parameters.md).
