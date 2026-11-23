"""MCP server that drives scantailor-ocr-cli."""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path
from typing import Any

import anyio
from mcp.server.mcpserver import Context, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from .cli import (
    ENV_VAR,
    RUN_TIMEOUT,
    cli_version,
    find_cli,
    is_capable,
    run_cli,
)
from .roots import allowed_roots, is_allowed, resolve_path

logger = logging.getLogger(__name__)


def _tool_error(message: str) -> None:
    raise ToolError(message)


def build_server(configured_roots: list[Path]) -> tuple[MCPServer, dict[str, Any]]:
    cli = find_cli()
    capable = False
    capability_message = ""
    if cli is None:
        capability_message = (
            "scantailor-ocr-cli was not found. Set " + ENV_VAR + ", install it, or add it to PATH."
        )
    else:
        capable, capability_message = is_capable(cli)
        if not capable:
            capability_message = (
                f"The found CLI ({cli}) is too old for this server: {capability_message}"
            )
        else:
            logger.info("Using CLI %s (%s)", cli, cli_version(cli))

    state: dict[str, Any] = {
        "cli": cli,
        "capable": capable,
        "capability_message": capability_message,
        "configured_roots": configured_roots,
    }

    server = MCPServer(
        name="scantailor-ocr",
        instructions=(
            "Drives scantailor-ocr-cli. Reads profiles and project information without "
            "side effects. Paths must lie inside an allowed folder."
        ),
    )

    async def _ensure_capable() -> None:
        if state["cli"] is None or not state["capable"]:
            _tool_error(state["capability_message"])

    async def _run_cli(args: list[str]) -> tuple[int, str, str]:
        cli_path: Path = state["cli"]
        return await anyio.to_thread.run_sync(run_cli, [str(cli_path), *args], RUN_TIMEOUT)

    @server.tool(
        description=(
            "List the profiles scantailor-ocr-cli can use. Reads only, creates nothing. "
            "No path input."
        )
    )
    async def profile_list(ctx: Context) -> dict[str, Any]:
        await _ensure_capable()
        rc, out, err = await _run_cli(["--profile-list"])
        if rc == 1:
            _tool_error(f"CLI usage error: {err.strip()}")
        if rc != 0:
            _tool_error(f"CLI failed (exit {rc}): {err.strip()}")
        try:
            data = json.loads(out)
        except json.JSONDecodeError:
            _tool_error("CLI did not produce valid JSON")
        if not isinstance(data, dict) or data.get("schemaVersion") != 1:
            _tool_error("CLI produced an unexpected schema")
        return {"state": "succeeded", "exit_code": 0, "profiles": data}

    @server.tool(
        description=(
            "Read a .ScanTailor project's pages and output folder. Reads only, creates "
            "nothing. The project path must lie inside an allowed folder."
        )
    )
    async def project_info(project: str, ctx: Context) -> dict[str, Any]:
        await _ensure_capable()
        if not project.lower().endswith(".scantailor"):
            _tool_error("project must end with .ScanTailor")
        roots = await allowed_roots(ctx, state["configured_roots"])
        if not roots:
            _tool_error(
                "No allowed folders. Pass --allow-root <folder> when starting the server, "
                "or let the client provide roots."
            )
        try:
            resolved = resolve_path(project)
        except (OSError, RuntimeError) as exc:
            _tool_error(f"Could not resolve path: {exc}")
        if not is_allowed(resolved, roots):
            _tool_error(f"Path is outside the allowed folders: {resolved}")
        rc, out, err = await _run_cli(["--project-info", str(resolved)])
        if rc == 1:
            _tool_error(f"CLI usage error: {err.strip()}")
        if rc == 2:
            return {"state": "refused", "exit_code": 2, "message": err.strip()}
        if rc != 0:
            _tool_error(f"CLI failed (exit {rc}): {err.strip()}")
        try:
            data = json.loads(out)
        except json.JSONDecodeError:
            _tool_error("CLI did not produce valid JSON")
        if not isinstance(data, dict) or data.get("schemaVersion") != 1:
            _tool_error("CLI produced an unexpected schema")
        return {"state": "succeeded", "exit_code": 0, "project": data}

    return server, state


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="scantailor-ocr-mcp")
    parser.add_argument(
        "--allow-root",
        action="append",
        default=[],
        metavar="FOLDER",
        help="Allowed folder (may be given more than once).",
    )
    parser.add_argument(
        "--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"]
    )
    return parser.parse_args(argv)


def main() -> None:
    args = parse_args()
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        stream=sys.stderr,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    configured = []
    for folder in args.allow_root:
        try:
            configured.append(Path(folder).resolve())
        except (OSError, RuntimeError) as exc:
            logger.error("Could not resolve allowed root %s: %s", folder, exc)
    server, _state = build_server(configured)
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
