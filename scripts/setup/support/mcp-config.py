"""Reads or sets one MCP server entry in a client's JSON config file.

Used by 06-setup-mcp.ps1 for the files of Claude Code (.claude.json), Claude Desktop
(claude_desktop_config.json) and Antigravity (mcp_config.json). All three keep their servers
as an object "mcpServers" of entries {"command", "args", "env"}; Claude Code adds "type" and
keeps the servers of one folder under "projects" -> <folder>. Everything else in the file is
kept as it is. Standard library only, run through uv:

    uv run --no-project python mcp-config.py get FILE KEY...
    uv run --no-project python mcp-config.py find FILE NAME
    uv run --no-project python mcp-config.py set FILE KEY... --command C [--arg=A ...]
        [--env NAME=VALUE ...] [--type stdio]

KEY... is the path to the entry, e.g. "mcpServers scantailor-ocr". "get" prints the entry as
JSON, or null when the file or the entry is missing. "find" lists the entries NAME of Claude
Code, for all projects and per folder, as a JSON list of {"scope", "folder", "entry"}. "set"
replaces the entry, after copying the file to FILE.<date-time>.bak; a file that does not exist
yet is created.
"""

import argparse
import json
import os
import shutil
import sys
import tempfile
from datetime import datetime


def load(path):
    if not os.path.exists(path):
        return {}
    # utf-8-sig: a file saved by an editor may start with a byte order mark.
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    if not text.strip():
        return {}
    data = json.loads(text)
    if not isinstance(data, dict):
        raise ValueError(f"{path} does not hold a JSON object")
    return data


def get_entry(data, keys):
    node = data
    for key in keys:
        if not isinstance(node, dict) or key not in node:
            return None
        node = node[key]
    return node


def find_entries(data, name):
    found = []
    user = get_entry(data, ["mcpServers", name])
    if user is not None:
        found.append({"scope": "user", "folder": "", "entry": user})
    projects = data.get("projects")
    if isinstance(projects, dict):
        for folder, project in projects.items():
            entry = get_entry(project, ["mcpServers", name])
            if entry is not None:
                found.append({"scope": "local", "folder": folder, "entry": entry})
    return found


def set_entry(data, keys, entry):
    node = data
    for key in keys[:-1]:
        child = node.get(key)
        if child is None:
            child = node[key] = {}
        elif not isinstance(child, dict):
            raise ValueError(f"'{key}' in the file is not a JSON object")
        node = child
    node[keys[-1]] = entry


def save(path, data):
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    if os.path.exists(path):
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        backup = f"{path}.{stamp}.bak"
        shutil.copy2(path, backup)
        print(f"Backup: {backup}")
    # Written next to the file and then moved over it, so a failure leaves the old file whole.
    fd, temp = tempfile.mkstemp(dir=folder, prefix=".mcp-config-", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write("\n")
        os.replace(temp, path)
    except BaseException:
        if os.path.exists(temp):
            os.remove(temp)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("action", choices=["get", "find", "set"])
    parser.add_argument("file")
    parser.add_argument("keys", nargs="+")
    parser.add_argument("--command")
    parser.add_argument("--arg", action="append", default=[])
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("--type")
    options = parser.parse_args()

    data = load(options.file)
    if options.action == "get":
        print(json.dumps(get_entry(data, options.keys), ensure_ascii=True))
        return 0
    if options.action == "find":
        print(json.dumps(find_entries(data, options.keys[0]), ensure_ascii=True))
        return 0

    if not options.command:
        parser.error("set needs --command")
    entry = {}
    if options.type:
        entry["type"] = options.type
    entry["command"] = options.command
    entry["args"] = options.arg
    env = {}
    for item in options.env:
        name, sep, value = item.partition("=")
        if not sep or not name:
            parser.error(f"--env needs NAME=VALUE: {item}")
        env[name] = value
    entry["env"] = env
    set_entry(data, options.keys, entry)
    save(options.file, data)
    print(f"Written: {options.file}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
