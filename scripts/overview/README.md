# Overview runs

Scripts that draw overviews of the code: a graph of how files, classes and functions use each
other, class and sequence diagrams, and the targets of the build. Unlike the [analysis runs](../analysis/README.md) they
look for no faults. Every script is one run; they are independent of each other.

The logs and diagrams go to `results\` in this folder, the graph of run 01 to `graphify-out\` in
the repository (both ignored by git). How the scripts are started, their common parameters and
their logs are described in [the README of the scripts](../README.md). Windows only.

## Overview

| Run | Script | What it does | Build folder | Output |
|---|---|---|---|---|
| 01 | `01-graphify.ps1` | graphify: the code as a graph, to browse and to query | – | `graphify-out\` in the repository, `results\01-graphify.log` |
| 02 | `02-clang-uml.ps1` | clang-uml: the class and sequence diagrams of `clang-uml.yml` | `build-overview` | `results\clang-uml\diagrams.html` (and `.md`, `*.mmd`), `results\02-clang-uml.log` |
| 03 | `03-cmake-graphviz.ps1` | `cmake --graphviz`: which targets of the build use which others | `build-overview` if configured, else `build-graphviz` (configured only) | `results\cmake-graphviz\graphs.html` (and `*.dot`, `*.svg`), `results\03-cmake-graphviz.log` |

## Requirements

The tools are set up by `scripts\setup\05-setup-overview.ps1`; its step 9 shows which are there,
in which version. Steps 3, 5 and 6 offer the version this setup was tested with or the newest one.

| Run | Needs | Set up by `05-setup-overview.ps1` |
|---|---|---|
| 01 | graphify, a Python program, as a tool of uv (`%USERPROFILE%\.local\bin\graphify.exe`) | step 1 (uv, with winget), step 2 (its folders), step 3 |
| 02 | clang-uml (`C:\Program Files\clang-uml`); for its first build Visual Studio and vcpkg as for the analysis runs | step 5, with the installer from GitHub (administrator rights) |
| 03 | Visual Studio (its CMake) and vcpkg; optionally Graphviz (`C:\Program Files\Graphviz`) for SVG files | step 6, Graphviz with winget (administrator rights) |

    powershell -ExecutionPolicy Bypass -File scripts\setup\05-setup-overview.ps1

uv keeps its Pythons and tools in `%USERPROFILE%\.local\share\uv` (step 2 sets
`UV_PYTHON_INSTALL_DIR` and `UV_TOOL_DIR`), not in `AppData`: a program installed as an app
package, such as the Claude desktop app, has its writes there redirected into a folder of its
own, and uv then can't find the Python it has just installed.

## The runs

### 01 – graphify

[graphify](https://github.com/Graphify-Labs/graphify) parses the code (C++, CMake, PowerShell
and more) with tree-sitter and builds a graph of files, classes and functions and of how they
call, include and inherit from each other, then groups it into communities. Only the code is
read and no language model is used (`--code-only`, `--no-label`): nothing leaves the machine.
Folders ignored by git are left out. Each community is named after the folder most of its code
is in and its most connected node, e.g. `core/filters/deskew/ui · OptionsWidget`
(`support\graphify-labels.py`); a community spread over several folders names its two biggest.

    powershell -ExecutionPolicy Bypass -File scripts\overview\01-graphify.ps1

- `graphify-out\graph.html` shows the graph in the browser, `GRAPH_REPORT.md` lists the most
  connected nodes and the communities, `COMMUNITIES_BY_FOLDER.md` for each folder the
  communities its code is in, to find those of the part of the code a review looks at.
- The command line reads `graph.json`, run in the repository:
  `graphify explain "<class or function>"` (its connections with file and line),
  `graphify path "<A>" "<B>"`, `graphify query "<question>"` (by key words) and
  `graphify god-nodes`. A name used in several files is answered with the list of node ids
  to choose from.
- **Claude Code:** with `/graphify` set up (setup step 4), `/graphify query`, `/graphify explain`
  and `/graphify path` answer from this graph. A plain `/graphify .` would build the graph
  itself, and also have the model read the documents and images, which costs many tokens.

Without `-Update` the graph is built anew; `-Update` reads only the files changed since the last
run. Some headers are read only in part (graphify reports "syntax errors" for them); their
classes are then missing or incomplete in the graph.

### 02 – clang-uml

[clang-uml](https://github.com/bkryza/clang-uml) reads the code with the Clang compiler and
draws the diagrams defined in [clang-uml.yml](clang-uml.yml), in Mermaid: each to
`results\clang-uml\<diagram>.mmd`, and all of them to two files in the same folder:

- `diagrams.html` shows them in any browser, opened with a double click. It loads Mermaid
  (version 12.3.0) from the internet when it is opened. The diagrams have their full size, as
  shrunk to the window the text of a large one can't be read. Each sits in a box at most as
  high as the window, with its scroll bars in view; dragging with the mouse moves it as well.
- `diagrams.md` holds them as `mermaid` blocks, which GitHub shows as diagrams, and VS Code
  with the extension "Markdown Preview Mermaid Support".

    powershell -ExecutionPolicy Bypass -File scripts\overview\02-clang-uml.ps1

clang-uml needs the compile database and the files the build generates (Qt's moc and uic).
Without them the run first configures and builds `build-overview` (a full build, once);
`-BuildDir build-analysis` uses the build of analysis run 01 instead. `-Diagram <names>` draws
only some of the diagrams.

| Diagram | Shows |
|---|---|
| `filters_classes` | the steps (`AbstractFilter` and its subclasses) and how `StageSequence` holds them |
| `processall_sequence` | what `StageSequence::processAll` calls: the processing the command line tool runs |

A new diagram is an entry under `diagrams:` in `clang-uml.yml`; the
[clang-uml documentation](https://clang-uml.github.io/) describes the diagram types and filters.
Paths there are relative to the repository. Notes on clang-uml 0.6.3:

- Filters that combine others (`anyof`, `allof`) only work with `filter_mode: advanced`; without
  it a class diagram takes in every type the classes use.
- `from` of a sequence diagram is the full signature, as clang-uml writes it
  (`Class::method(const Type &,int) const`). If it doesn't match, the error message suggests
  the closest name.
- A class template in the same translation unit gives the methods of a class its name
  ([bkryza/clang-uml#461](https://github.com/bkryza/clang-uml/issues/461)); excluding the
  header of the template with `exclude: paths:` avoids it, see `processall_sequence`.
- `--print-from`, which should list the possible start points, fails with "invalid map<K, T>
  key" as soon as a call stands in a condition
  ([bkryza/clang-uml#462](https://github.com/bkryza/clang-uml/issues/462)).
- clang-uml ends with exit code 0 also when it could not draw a diagram; the run checks that
  every diagram was written and ends with exit code 1 otherwise.

### 03 – cmake --graphviz

CMake writes the targets of the build (programs, libraries, tests) and the libraries they link
as graphs in the DOT language of [Graphviz](https://graphviz.org/). `-Graph` chooses which,
default all three:

| Graph | Shows |
|---|---|
| `Program` | `scantailor-ocr` and `scantailor-ocr-cli` with the libraries of the project they use: `core`, `core_gui`, the libraries of the steps and their `_ui` libraries |
| `Targets` | all targets of the project, with the tests (`*_tests`) and the developer tools |
| `All` | all targets and the libraries they link from outside (Qt, Boost, Tesseract, Windows) |

    powershell -ExecutionPolicy Bypass -File scripts\overview\03-cmake-graphviz.ps1 -Graph Program

Each graph goes to `results\cmake-graphviz\<graph>.dot`, all of them to `graphs.html` there,
which draws them like the page of run 02 (it loads viz.js, Graphviz for the browser, version
3.31.0, from the internet). With Graphviz installed (setup step 6) each is also drawn to
`<graph>.svg`, to be seen offline. The shape of a node says what kind of target it is (see the
legend in each graph): an egg a program, an octagon a static library.

CMake needs a configured build folder, not a built one: the run uses `build-overview` of
run 02 if it is configured, else it only configures `build-graphviz`. Its settings for the
graphs are written to `CMakeGraphVizOptions.cmake` in the build folder while it runs, and
removed at the end.
