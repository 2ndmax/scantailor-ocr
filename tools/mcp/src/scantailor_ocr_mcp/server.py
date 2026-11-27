"""MCP server that drives scantailor-ocr-cli."""

from __future__ import annotations

import argparse
import logging
import subprocess
import sys
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any, NoReturn

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
from .jobs import JobManager
from .roots import allowed_roots, resolve_allowed_path
from .validation import validate_cli_json

logger = logging.getLogger(__name__)


def _tool_error(message: str) -> NoReturn:
    raise ToolError(message)


def build_server(
    configured_roots: list[Path], max_runs: int = 1
) -> tuple[MCPServer, dict[str, Any]]:
    cli = find_cli()
    capable = False
    capability_message = ""
    if cli is None:
        capability_message = (
            "scantailor-ocr-cli was not found. Set " + ENV_VAR + ", install it, or add it to PATH."
        )
    else:
        capable, capability_message = is_capable(cli)
        if capable:
            logger.info("Using CLI %s (%s)", cli, cli_version(cli))

    job_manager = JobManager(cli, capable, capability_message, configured_roots, max_runs)

    state: dict[str, Any] = {
        "cli": cli,
        "capable": capable,
        "capability_message": capability_message,
        "configured_roots": configured_roots,
        "max_runs": max_runs,
        "job_manager": job_manager,
    }

    @asynccontextmanager
    async def lifespan(server: MCPServer) -> Any:
        yield {}
        await job_manager.shutdown()

    server = MCPServer(
        name="scantailor-ocr",
        instructions=(
            "Drives scantailor-ocr-cli. Reads profiles and project information without "
            "side effects, and runs whole books as background jobs. Paths must lie inside "
            "an allowed folder. A project open in the ScanTailor app must not be processed "
            "here; reopen it in the app afterwards."
        ),
        lifespan=lifespan,
    )

    async def _ensure_capable() -> None:
        if state["cli"] is None or not state["capable"]:
            _tool_error(state["capability_message"])

    async def _run_cli(args: list[str]) -> tuple[int, str, str]:
        cli_path: Path = state["cli"]
        try:
            return await anyio.to_thread.run_sync(run_cli, [str(cli_path), *args], RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            _tool_error(f"CLI command timed out after {RUN_TIMEOUT:g}s")
        except OSError as exc:
            _tool_error(f"Could not run CLI: {exc}")

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
            data = validate_cli_json(out)
        except ValueError as exc:
            _tool_error(str(exc))
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
            resolved = resolve_allowed_path(project, roots)
        except ValueError as exc:
            _tool_error(str(exc))
        rc, out, err = await _run_cli(["--project-info", str(resolved)])
        if rc == 1:
            _tool_error(f"CLI usage error: {err.strip()}")
        if rc == 2:
            return {"state": "refused", "exit_code": 2, "message": err.strip()}
        if rc != 0:
            _tool_error(f"CLI failed (exit {rc}): {err.strip()}")
        try:
            data = validate_cli_json(out)
        except ValueError as exc:
            _tool_error(str(exc))
        return {"state": "succeeded", "exit_code": 0, "project": data}

    @server.tool(
        description=(
            "Start a run of scantailor-ocr-cli and return immediately with a job_id. "
            "Either 'folder' (create a new project) or 'project' (an existing .ScanTailor) "
            "is required, never both. Poll run_status for progress; cancel with run_cancel. "
            "Do not process a project that is open in the ScanTailor app. Creation option "
            "'orientation_detect' ('pages' or 'book', folder only) detects page orientation "
            "before the first step: 'pages' rotates only pages whose orientation is certain, "
            "'book' also rotates uncertain pages like their neighbors or the majority; the "
            "report then carries 'rotationDetection' per page ('undecided' pages need a manual "
            "check)."
        )
    )
    async def run_start(
        folder: str | None = None,
        project: str | None = None,
        profile: str | None = None,
        dpi: str | None = None,
        dpi_fix: str | None = None,
        project_file: str | None = None,
        output_folder: str | None = None,
        color_mode_detect: bool = False,
        orientation_detect: str | None = None,
        last_step: str | None = None,
        threads: int | None = None,
        export_folder: str | None = None,
        export_name: str | None = None,
        export_formats: list[str] | None = None,
        ctx: Context = None,  # type: ignore[assignment]
    ) -> dict[str, Any]:
        job = await job_manager.start_job(
            ctx,
            folder=folder,
            project=project,
            profile=profile,
            dpi=dpi,
            dpi_fix=dpi_fix,
            project_file=project_file,
            output_folder=output_folder,
            color_mode_detect=color_mode_detect,
            orientation_detect=orientation_detect,
            last_step=last_step,
            threads=threads,
            export_folder=export_folder,
            export_name=export_name,
            export_formats=export_formats,
        )
        return {"job_id": job.id, "state": "running"}

    @server.tool(
        description=(
            "Return the state of a run started with run_start: 'running' with step/done/total, "
            "or a terminal state. Terminal states 0/3/4/5 carry the full report; 1/2 carry a "
            "message."
        )
    )
    async def run_status(job_id: str, ctx: Context) -> dict[str, Any]:
        job = job_manager.get_job(job_id)
        return job.status()

    @server.tool(
        description=(
            "Cancel a running job. Returns immediately; the final result is available through "
            "run_status (state 'cancelled', exit_code 5). A finished job is not an error."
        )
    )
    async def run_cancel(job_id: str, ctx: Context) -> dict[str, Any]:
        job = job_manager.get_job(job_id)
        if job.finished.is_set():
            return {"job_id": job.id, "state": job.state, "cancel_requested": False}
        await job.cancel()
        return {"job_id": job.id, "state": "running", "cancel_requested": True}

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
        "--max-runs", type=int, default=1, metavar="N", help="Maximum concurrent runs (default 1)."
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
    max_runs = args.max_runs if args.max_runs and args.max_runs > 0 else 1
    server, _state = build_server(configured, max_runs=max_runs)
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
