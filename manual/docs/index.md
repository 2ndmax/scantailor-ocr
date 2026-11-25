---
title: Overview
hide:
  - navigation
---

# ScanTailor OCR Manual

ScanTailor OCR turns raw scans of books and documents into clean pages and a
**compact, searchable PDF**. You give it the images from your scanner; it straightens
them, crops them, removes dirt and shadows, converts them to crisp black and white
where that fits, recognizes the text and writes the PDF.

![The main window of ScanTailor OCR with a page of Riemann's collected works](images/main-window.png){ .shot }

## How it works in one sentence

A project goes through **seven steps**, one after the other. Each step does one job
and shows you its result, so you can check and correct it before the next step
builds on it.

<div class="steps" markdown>
[**1 Import** <span>Rotate, trim, import new scans</span>](steps/import.md)
[**2 Split Pages** <span>Split two-page scans</span>](steps/split-pages.md)
[**3 Deskew** <span>Straighten tilted pages</span>](steps/deskew.md)
[**4 Select Content** <span>Find the page and its text</span>](steps/select-content.md)
[**5 Margins** <span>Margins and common page size</span>](steps/margins.md)
[**6 Output** <span>Clean up, black and white, dewarp</span>](steps/output.md)
[**7 Export** <span>PDF, OCR, TXT, hOCR, ALTO</span>](steps/export.md)
</div>

## Where to start

- **New to ScanTailor?** Read the [Quick Start](getting-started/quick-start.md). It takes
  you from a folder of scans to a PDF in a few minutes, with the default settings.
- **Want to understand a setting?** Every step has its own page under
  [The Steps](steps/index.md).
- **Curious how the methods differ?** [Methods Explained](methods/index.md) shows what
  the binarization methods, filters, dewarping and compression actually do, with
  examples.
- **Processing many books?** The [command line tool](reference/command-line.md) runs a
  whole project without a window.

!!! tip "Tooltips"
    Almost every button, field and check box in ScanTailor OCR has a tooltip. Hover over
    it with the mouse when you are unsure what it does.

## About the sample pages

The screenshots and examples in this manual use pages of *Bernhard Riemann's
Gesammelte mathematische Werke* (1876), which are in the public domain.
