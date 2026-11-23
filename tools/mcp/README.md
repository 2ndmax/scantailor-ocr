# ScanTailor OCR MCP server

A [Model Context Protocol](https://modelcontextprotocol.io) server that drives
`scantailor-ocr-cli` from an agent (Claude Code, Claude Desktop). The server
does not process pages itself: it starts the CLI as a subprocess for every run
and reports what the CLI reports. It never reads or writes a project file
(`.ScanTailor`) itself; everything it knows about a project comes from the
CLI's `--project-info`.

## Prerequisites

- A built or installed `scantailor-ocr-cli` from this repository's state, one
  that understands `--profile-list` and `--project-info`.
- `uv` (0.13.9 or newer). `uv` fetches Python 3.11 or newer and the package
  dependencies on its own; no separate Python installation is needed.

## Finding the CLI

The server looks for the CLI in this order:

1. The environment variable `SCANTAILOR_OCR_CLI`, if set and pointing at an
   existing file.
2. `PATH` (`scantailor-ocr-cli` or `scantailor-ocr-cli.exe`).

There is no single standard install location, so that step is skipped. If the
variable points at a missing file, the server logs a warning and falls back to
`PATH`.

At startup the server checks the CLI by running `--profile-list` and validating
its JSON. If the CLI is missing, it starts anyway and every tool reports an
error. If the CLI is present but too old (it does not support `--profile-list`
with schema version 1), every tool reports "too old" instead.

## Starting the server

```
uv run --directory <repo>/tools/mcp scantailor-ocr-mcp [options]
```

Options (from `--help`):

- `--allow-root FOLDER` -- an allowed folder; may be given more than once.
- `--max-runs N` -- maximum concurrent runs; default 1.
- `--log-level {DEBUG,INFO,WARNING,ERROR}` -- default `INFO`.

Without at least one allowed folder the server still starts, but `project_info`
and `run_start` refuse every path: there is no folder they are allowed to touch.

**Allowed folders.** A path is allowed when it lies inside a folder the client
supplies through MCP `roots`, or inside a folder given with `--allow-root`. Every
path an agent passes (input folder, project file, output folder, export folder,
a profile file) is resolved to an absolute, normalised path with symbolic links
followed before the check; a path that escapes an allowed folder is refused.
Without any allowed folder nothing that takes a path works. The server logs the
roots it gets from the client, or that the client provides none.

The report of a run is written to a temporary folder, never to a folder of the
user, and removed as soon as the server has read it.

All server logging goes to standard error. Standard output carries only the MCP
protocol.

## Registering in Claude Code

```
claude mcp add --env SCANTAILOR_OCR_CLI=/path/to/scantailor-ocr-cli --transport stdio scantailor-ocr -- uv run --directory /path/to/repo/tools/mcp scantailor-ocr-mcp --allow-root /path/to/books
```

Claude Code's own options come before the server name; `--` separates them from
the command that starts the server, which gets everything after it untouched.
`--env` takes several `KEY=value` pairs, so another option such as
`--transport stdio` stands between it and the name, which would otherwise be
read as one more pair. `claude mcp add --help` shows the options of your
version.

On Windows, with absolute paths:

```
claude mcp add --env SCANTAILOR_OCR_CLI=C:\path\to\scantailor-ocr-cli.exe --transport stdio scantailor-ocr -- uv run --directory C:\path\to\repo\tools\mcp scantailor-ocr-mcp --allow-root C:\path\to\books
```

## Registering in Claude Desktop

Edit `claude_desktop_config.json` (open it from the app: Settings, Developer,
Edit Config, which creates it if missing):

- macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`
- Windows: `%APPDATA%\Claude\claude_desktop_config.json`

```json
{
  "mcpServers": {
    "scantailor-ocr": {
      "command": "C:\\path\\to\\uv.exe",
      "args": [
        "run",
        "--directory",
        "C:\\path\\to\\repo\\tools\\mcp",
        "scantailor-ocr-mcp",
        "--allow-root",
        "C:\\path\\to\\bookfolder"
      ],
      "env": {
        "SCANTAILOR_OCR_CLI": "C:\\path\\to\\scantailor-ocr-cli.exe"
      }
    }
  }
}
```

Use absolute paths for both `uv` and the CLI: desktop apps usually do not see
the shell's `PATH` (`where uv` on Windows, `which uv` elsewhere, shows the path
of `uv`). The config is read only when Claude Desktop starts, so restart the app
after editing.

## The tools

**`profile_list`** (no input). Returns the profile a run without `--profile`
uses and the profiles a run can use (`Default`, `Minimal`, plus file profiles),
with their source (`built-in` or `file`). Read only, creates nothing.

**`project_info`** (`project`: a `.ScanTailor` file). Returns the output
folder and the pages (file, page number, half, DPI, color mode, output file).
Read only, creates nothing. Refused with `state: refused` when the project
cannot be read.

**`run_start`** (`folder` or `project`, plus options). Starts a run and returns
a `job_id` immediately; poll with `run_status`. Either `folder` (create a new
project) or `project` (an existing `.ScanTailor`), never both. Creation options
(`profile`, `dpi`, `dpi_fix`, `project_file`, `output_folder`,
`color_mode_detect`) are only valid with `folder`; `profile` is a name from
`profile_list` or a `.stp` file in an allowed folder. Common options: `last_step`
(one of `fix-orientation`, `split-pages`, `deskew`, `select-content`,
`margins`, `output`, `export`; default `output`), `threads`, `export_folder`,
`export_name`, `export_formats` (`pdf`, `txt`, `hocr`, `alto`). A second run of
a project that is still running, or more runs than `--max-runs`, is refused
with `isError`.

**`run_status`** (`job_id`). Returns the state:

| state | exit code | meaning |
|---|---|---|
| `running` | (none) | in progress; carries `step`, `done`, `total`, `passes_started`, `cancel_requested` |
| `succeeded` | 0 | finished |
| `pages_failed` | 3 | some pages failed |
| `export_failed` | 4 | export failed |
| `cancelled` | 5 | cancelled |
| `refused` | 1 or 2 | CLI refused the run; carries `message` |
| `crashed` | other | the CLI ended unexpectedly, or without its report; carries `message` |

The states for exit codes 0, 3, 4 and 5 carry the full report under `report`.

**`run_cancel`** (`job_id`). Cancels a running job like Ctrl+C; the project and
the report are saved. Returns immediately; the final result is available through
`run_status`. A finished job is not an error.

Path inputs must lie inside an allowed folder, otherwise the tool fails with
`isError`.

The CLI's JSON (camelCase keys) is returned unchanged under `profiles` /
`project` / `report`; the server's own keys are snake_case.

The field definitions live in `src/core/CommandLineRun.h`; this README does not
repeat them.

## A typical run for an agent

1. `profile_list` to see the available profiles.
2. `run_start` with a folder (or an existing project).
3. `run_status` in intervals until the state is no longer `running`.
4. Read the result and the errors from `report`.

## What the server deliberately does not do

- No network access.
- No paths outside the allowed folders.
- No queue: at most `--max-runs` runs at once (default 1), because each run
  already uses all cores.
- No project that is open in the ScanTailor app: the app does not notice the
  changes and would overwrite them when it saves. Reopen the project in the app
  after a run.
- The CLI's messages (`message`) come in the language configured in the app.

## Cancelling and shutting down

- `run_cancel` stops the run like Ctrl+C; the project and the report are saved.
- When the server ends, it cancels running runs and waits up to 30 seconds for
  them to save.
- On Windows the cancellation needs a console. If the server starts without one,
  it allocates a hidden console first.

## Troubleshooting

- **CLI not found.** Set `SCANTAILOR_OCR_CLI` to the binary, or put it on
  `PATH`. The server still starts without a CLI, but every tool fails.
- **"too old".** The CLI predates `--profile-list` (schema version 1). Build
  from this repository's state.
- **Path refused.** The path is outside every allowed folder. Add the folder
  with `--allow-root`, or let the client provide it through MCP `roots`.
- **Logs.** The server logs to standard error, more with `--log-level DEBUG`.
  Where a client shows them depends on the client.

## Development and tests

See [TESTING.md](../../TESTING.md) (section on the server tests) for
`uv run pytest` in `tools/mcp/` and the CI that runs them.

## Tested clients

Results of the hand tests follow.
</content>
