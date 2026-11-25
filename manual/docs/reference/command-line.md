# Command Line Tool

`scantailor-ocr-cli` processes a project without opening a window, for example to run
many books one after the other, or on a server. It does exactly what the batch processing
of the app does, with the same results.

```text
scantailor-ocr-cli [options] <folder or project file>
```

The tool's messages use the language set in the app. `--help` lists all options.

## Two modes

=== "Existing project"

    ```text
    scantailor-ocr-cli Book.ScanTailor
    ```

    Processes the pages of the project, with its own settings, and saves the project.
    Prepare the project in the app, then let the tool do the long work.

=== "Folder"

    ```text
    scantailor-ocr-cli C:\Scans\Book
    ```

    First creates a project from the images in the folder, as the *Project Files* window
    of the app does, then processes it. The images of the folder itself are taken (not
    of subfolders, not hidden files), sorted as in the app. The project file is
    `<folder>\<folder name>.ScanTailor`, the output goes to `<folder>\out`.

    An existing project file is never overwritten: the tool refuses and names the call
    that processes it.

## Options

### Creating the project (folder mode only)

| Option | Meaning |
|---|---|
| `--profile <name or file>` | The start values: `Default`, `Minimal`, `Custom`, a saved profile, or a `.stp` file. Without it, the profile set in the app is used. |
| `--dpi <n or HxV>` | The DPI of all images, e.g. `300` or `600x300`. |
| `--dpi-fix <n or HxV>` | The DPI of only the images that need fixing (red and yellow in *Fix DPI*). |
| `--project-file <file>` | The project file instead of the default. |
| `--output-folder <folder>` | The output folder instead of `<folder>\out`. |
| `--orientation-detect <pages or book>` | Detects the orientation of all pages before the first step. `pages` rotates pages whose orientation is certain; `book` also rotates uncertain pages like their neighbors or the majority. |
| `--color-mode-detect` | Detects the color mode of all pages after *Margins*, as *Detect Color Mode* in the app does. Needs `--last-step margins` or later. |

Without `--dpi` or `--dpi-fix`, images without a usable resolution are refused and
doubtful ones listed as a warning.

### Steps and export

| Option | Meaning |
|---|---|
| `--last-step <step>` | Where the run ends: `fix-orientation`, `split-pages`, `deskew`, `select-content`, `margins`, `output` (default) or `export`. |
| `--export-folder <folder>` | The folder for the exported files instead of the folder of the project file. |
| `--export-name <name>` | The file name of the export, without extension. |
| `--export-formats <list>` | The formats instead of those ticked in the project: `pdf`, `txt`, `hocr`, `alto`, separated by commas or plus signs. |

The export options lead the run to the export by themselves; `--last-step export` does
the same with the settings of the project alone.

### The whole run

| Option | Meaning |
|---|---|
| `--threads <n>` | The number of threads of this run, instead of the setting of the app. |
| `--report-file <file.json>` | Writes a JSON report at the end: project, profile, last step, exit code, and for each page its file, resolution, color mode, output file and status, plus the exported files. |
| `--progress-json` | The progress as JSON lines on the standard output, one per event; all other text goes to the standard error. |
| `--profile-list` | Lists the profiles `--profile` takes, as JSON, and names the one used without it. |
| `--project-info` | Describes the pages of a project (file, DPI, color mode, output file) as JSON, without processing anything. |

## Examples

Process a folder of scans at 400 DPI and export a PDF with text layer:

```text
scantailor-ocr-cli --dpi 400 --export-formats pdf C:\Scans\Book
```

The OCR languages, the text layer and the compression come from the profile, here the
one set in the app.

Process an existing project up to *Margins* only, to check it in the app before the
output is made:

```text
scantailor-ocr-cli --last-step margins Book.ScanTailor
```

Export a prepared project as PDF and ALTO into another folder:

```text
scantailor-ocr-cli --export-folder D:\Export --export-formats pdf,alto Book.ScanTailor
```

## Checks before the run

Everything that can be checked is checked **before** processing starts, so a long run
isn't lost at its end: the project file and the report must be writable, the output and
export folders writable or possible to create, and the OCR languages installed.

The tool refuses what the app would ask about, instead of guessing:

- projects saved by ScanTailor Advanced or an older version (open and save them once in
  the app),
- images without a resolution (set it in the app, or pass `--dpi`),
- projects whose images or output folder are missing (relink them in the app),
- OCR languages that aren't installed (the tool downloads nothing),
- an export that would replace the project file or one of its images.

!!! warning "Not while the project is open in the app"
    Don't process a project with the tool while it is open in the app. The app doesn't
    notice the changes and would overwrite them when it saves. Reopen the project in the
    app afterwards.

## Canceling

++ctrl+c++ starts no further page and lets the pages already running finish. The pages
done keep their output, and the project is saved with them. On Windows, ++ctrl+break++
cancels the same way.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Done |
| 1 | Wrong command line |
| 2 | Input unusable (project, folder, profile, resolution, OCR languages) or project not saved |
| 3 | Pages failed at the last step |
| 4 | Export failed |
| 5 | Canceled |
