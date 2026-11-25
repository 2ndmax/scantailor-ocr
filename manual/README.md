# ScanTailor OCR Manual

The user manual of ScanTailor OCR, in English, built with
[MkDocs](https://www.mkdocs.org) and [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/).
The Pages workflow (`.github/workflows/pages.yml`) builds it into `website/manual`, so the
website serves it under `/manual/`.

## Folders

| Path | Content |
|---|---|
| `mkdocs.yml` | Site configuration and navigation |
| `requirements.txt` | Pinned versions of MkDocs and Material |
| `docs/` | The pages (Markdown), `docs/images/` the screenshots, `docs/assets/` logo and CSS |
| `includes/abbreviations.md` | Tooltips for abbreviations, appended to every page |

## Build and preview

With [uv](https://docs.astral.sh/uv/), no separate Python installation is needed. In this
folder:

```
uvx --from mkdocs --with-requirements requirements.txt mkdocs serve
uvx --from mkdocs --with-requirements requirements.txt mkdocs build --strict
```

`serve` shows the manual at http://127.0.0.1:8000/scantailor-ocr/manual/ and rebuilds on
every change. `build --strict` writes `site/` (not committed) and fails on broken links.

## Screenshots

- Program screenshots: the window of ScanTailor OCR (dark color scheme, English UI), taken
  at full resolution with Windows' `PrintWindow`, using pages of the public domain book
  Riemann, *Gesammelte mathematische Werke* (1876). The project lies under the neutral
  path `S:\Scans\riemann` (a `subst` drive), so no personal folder shows up.
- `docs/images/binarization/`: one page of that book with an artificial shadow and stain
  added, processed by `scantailor-ocr-cli` once per binarization method, with
  *Equalize illumination* off and default values, then cropped to the same region.

When a visible text of the program changes, check the pages and screenshots that show it.

Note: Material for MkDocs is in maintenance mode until 2027; its successor
[Zensical](https://zensical.org) reads the same `mkdocs.yml`.
