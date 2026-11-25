# MCP Server

The MCP server lets an AI agent, such as Claude Code or Claude Desktop, drive the
[command line tool](command-line.md): list the profiles, describe a project, and start,
follow and cancel runs. You can then ask the agent things like *"Process the scans in
my folder Book and make a searchable PDF"*.

The server doesn't process pages itself. It starts `scantailor-ocr-cli` for each run and
reports what the tool reports.

## What the agent can do

| Tool | Does |
|---|---|
| `profile_list` | Lists the profiles a run can use. |
| `project_info` | Describes the pages of a project (file, DPI, color mode, output file). |
| `run_start` | Starts a run on a folder or a project, with the options of the command line tool. |
| `run_status` | Reports the progress and, at the end, the result. |
| `run_cancel` | Cancels a run like ++ctrl+c++. |

## Safety

- The server only touches folders you allow, given with `--allow-root` or by the
  client. Every other path is refused.
- It has no network access.
- At most one run at a time by default, since each run already uses all processor
  cores.
- Don't let it process a project that is open in the app.

## Setting it up

The server is in the folder `tools/mcp` of the source code and needs
[uv](https://docs.astral.sh/uv/). How to register it in Claude Code or Claude Desktop is
described in its
[README](https://github.com/2ndmax/scantailor-ocr/blob/master/tools/mcp/README.md).
