"""Run jobs: start, status, cancel, and shutdown cleanup."""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid
from collections import deque
from pathlib import Path
from typing import Any, NoReturn

import anyio
from mcp.server.mcpserver.exceptions import ToolError

from .console import ensure_console
from .roots import allowed_roots, normalize_path_key, resolve_allowed_path
from .validation import validate_cli_json

logger = logging.getLogger(__name__)

LAST_STEPS = {
    "fix-orientation",
    "split-pages",
    "deskew",
    "select-content",
    "margins",
    "output",
    "export",
}
EXPORT_FORMATS = {"pdf", "txt", "hocr", "alto"}
MAX_STDERR_TAIL = 64 * 1024
MAX_PROGRESS_EVENTS = 1000
MAX_FINISHED_JOBS = 100
CANCEL_WAIT_TIMEOUT = 30.0


def _tool_error(message: str) -> NoReturn:
    raise ToolError(message)


def _resolve_allowed(raw: str | Path, roots: list[Path]) -> Path:
    try:
        return resolve_allowed_path(raw, roots)
    except ValueError as exc:
        _tool_error(str(exc))


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _parse_int(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return default
    return default


def parse_dpi_value(value: str) -> bool:
    parts = value.strip().lower().split("x")
    if not parts or len(parts) > 2 or any(not p for p in parts):
        return False
    for part in parts:
        if not part.isascii() or not part.isdigit():
            return False
        if int(part) <= 0:
            return False
    return True


def default_project_file(folder: Path) -> Path:
    name = folder.name
    if not name:
        name = "project"
    return (folder / (name + ".ScanTailor")).resolve()


class Job:
    """One running or finished CLI run."""

    def __init__(
        self, job_id: str, cli: Path, project_key: str, report_dir: Path, report_path: Path
    ):
        self.id = job_id
        self.cli = cli
        self.project_key = project_key
        self.report_dir = report_dir
        self.report_path = report_path
        self.args: list[str] = []
        self.process: Any = None
        self.stdout_task: asyncio.Task | None = None
        self.stderr_task: asyncio.Task | None = None
        self.wait_task: asyncio.Task | None = None
        self.finished = asyncio.Event()

        self.state = "running"
        self.exit_code: int | None = None
        self.step: str | None = None
        self.done: int = 0
        self.total: int = 0
        self.passes_started: int = 0
        self.cancel_requested: bool = False
        self.cancel_signal_sent: bool = False
        self.progress_events: deque[dict[str, Any]] = deque(maxlen=MAX_PROGRESS_EVENTS)
        self.stderr_tail: str = ""
        self.report: dict[str, Any] | None = None
        self.message: str | None = None

    async def _read_stdout(self) -> None:
        buf = b""
        try:
            while True:
                try:
                    data = await self.process.stdout.receive(4096)
                except Exception:
                    break
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.rstrip(b"\r")
                    if not line.strip():
                        continue
                    text = line.decode("utf-8", "replace")
                    try:
                        obj = json.loads(text)
                    except json.JSONDecodeError:
                        logger.warning("non-JSON stdout line from CLI: %s", text)
                        continue
                    if not isinstance(obj, dict):
                        logger.warning("non-dict JSON stdout line from CLI: %s", text)
                        continue
                    self.progress_events.append(obj)
                    self._update_progress(obj)
        finally:
            if buf.strip():
                text = buf.decode("utf-8", "replace").strip()
                if text:
                    try:
                        obj = json.loads(text)
                        if isinstance(obj, dict):
                            self.progress_events.append(obj)
                            self._update_progress(obj)
                        else:
                            logger.warning("non-dict JSON stdout line from CLI: %s", text)
                    except json.JSONDecodeError:
                        logger.warning("non-JSON stdout line from CLI: %s", text)

    def _update_progress(self, obj: dict[str, Any]) -> None:
        event = obj.get("event")
        if event == "pass-start":
            self.passes_started += 1
            if "step" in obj and isinstance(obj.get("step"), str):
                self.step = obj.get("step")
            if "total" in obj:
                self.total = _parse_int(obj.get("total"))
            self.done = 0
        elif event == "page":
            if "step" in obj and isinstance(obj.get("step"), str):
                self.step = obj.get("step")
            if "done" in obj:
                self.done = _parse_int(obj.get("done"))
            if "total" in obj:
                self.total = _parse_int(obj.get("total"))
        elif event == "export":
            self.step = "export"
            if "done" in obj:
                self.done = _parse_int(obj.get("done"))
            if "total" in obj:
                self.total = _parse_int(obj.get("total"))

    async def _read_stderr(self) -> None:
        buf = b""
        try:
            while True:
                try:
                    data = await self.process.stderr.receive(4096)
                except Exception:
                    break
                if not data:
                    break
                buf += data
                if len(buf) > MAX_STDERR_TAIL * 2:
                    buf = buf[-MAX_STDERR_TAIL:]
        finally:
            text = buf.decode("utf-8", "replace")
            if len(text) > MAX_STDERR_TAIL:
                text = text[-MAX_STDERR_TAIL:]
            self.stderr_tail = text

    async def _wait(self) -> None:
        try:
            if self.process is None:
                self.state = "crashed"
                self.message = "CLI process was not started"
                return
            try:
                await self.process.wait()
            except Exception as exc:  # pragma: no cover - defensive
                logger.exception("waiting for CLI failed: %s", exc)
            self.exit_code = self.process.returncode
            # make sure readers are done
            for task in (self.stdout_task, self.stderr_task):
                if task is not None and not task.done():
                    with contextlib.suppress(Exception):
                        await task
            with contextlib.suppress(Exception):
                await self.process.aclose()
            try:
                await self._finalize()
            except Exception as exc:
                logger.exception("finalizing CLI job %s failed: %s", self.id, exc)
                self.state = "crashed"
                self.message = str(exc) or "Failed to finalize CLI run"
        finally:
            self.finished.set()

    async def _finalize(self) -> None:
        report = None
        if self.report_path.exists():
            try:
                report = validate_cli_json(self.report_path.read_text(encoding="utf-8"))
            except (ValueError, OSError, json.JSONDecodeError) as exc:
                logger.warning("could not read report %s: %s", self.report_path, exc)
                report = None
        self.report = report
        # The report is in memory now. Its folder goes at once, as a client may end the server
        # without letting it clean up.
        shutil.rmtree(self.report_dir, ignore_errors=True)

        rc = self.exit_code
        stderr = self.stderr_tail.strip()
        if rc == 0:
            self.state = "succeeded" if report is not None else "crashed"
        elif rc == 3:
            self.state = "pages_failed" if report is not None else "crashed"
        elif rc == 4:
            self.state = "export_failed" if report is not None else "crashed"
        elif rc == 5:
            self.state = "cancelled" if report is not None else "crashed"
        elif rc in (1, 2):
            self.state = "refused"
        else:
            self.state = "crashed"

        if self.state == "crashed":
            self.message = stderr or f"CLI exited with code {rc} without a report"
        elif self.state == "refused":
            self.message = stderr or f"CLI refused inputs with exit code {rc}"

    async def _send_cancel(self) -> None:
        if self.process is None:
            return
        try:
            if sys.platform == "win32":
                self.process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                self.process.send_signal(signal.SIGINT)
        except (ProcessLookupError, OSError) as exc:
            logger.warning("could not send cancel signal: %s", exc)

    async def cancel(self) -> None:
        if self.finished.is_set():
            self.cancel_requested = False
            return
        if not self.cancel_signal_sent:
            self.cancel_signal_sent = True
            self.cancel_requested = True
            await self._send_cancel()
        else:
            self.cancel_requested = True

    def status(self) -> dict[str, Any]:
        if self.state == "running":
            return {
                "job_id": self.id,
                "state": "running",
                "exit_code": None,
                "step": self.step,
                "done": self.done,
                "total": self.total,
                "passes_started": self.passes_started,
                "cancel_requested": self.cancel_requested,
            }
        result: dict[str, Any] = {
            "job_id": self.id,
            "state": self.state,
            "exit_code": self.exit_code,
        }
        if self.state in ("succeeded", "pages_failed", "export_failed", "cancelled"):
            result["report"] = self.report
        elif self.state in ("refused", "crashed"):
            result["message"] = self.message
        return result

    async def kill(self) -> None:
        try:
            if self.process is not None:
                with contextlib.suppress(Exception):
                    self.process.kill()
            for task in (self.stdout_task, self.stderr_task, self.wait_task):
                if task is not None and not task.done():
                    task.cancel()
                    with contextlib.suppress(asyncio.CancelledError, Exception):
                        await task
            if self.process is not None:
                with contextlib.suppress(Exception):
                    await self.process.wait()
                with contextlib.suppress(Exception):
                    await self.process.aclose()
        finally:
            if self.state == "running":
                self.state = "crashed"
                self.message = self.message or "CLI process was killed"
            self.finished.set()


class JobManager:
    def __init__(
        self,
        cli: Path | None,
        capable: bool,
        capability_message: str,
        configured_roots: list[Path],
        max_runs: int,
        max_finished_jobs: int = MAX_FINISHED_JOBS,
    ):
        self.cli = cli
        self.capable = capable
        self.capability_message = capability_message
        self.configured_roots = configured_roots
        self.max_runs = max_runs
        self.max_finished_jobs = max_finished_jobs
        self.jobs: dict[str, Job] = {}
        self._console_ready = False
        self._shutting_down = False

    def _ensure_capable(self) -> None:
        if self.cli is None or not self.capable:
            _tool_error(self.capability_message)

    def _prune_finished_jobs(self) -> None:
        if self.max_finished_jobs < 0:
            return
        finished_ids = [
            jid
            for jid, job in self.jobs.items()
            if job.state != "running" and job.finished.is_set()
        ]
        excess = len(finished_ids) - self.max_finished_jobs
        for jid in finished_ids[: max(0, excess)]:
            del self.jobs[jid]

    async def start_job(
        self,
        ctx: Any,
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
    ) -> Job:
        self._ensure_capable()
        if self._shutting_down:
            _tool_error("Server is shutting down")
        self._prune_finished_jobs()

        # source check
        has_folder = folder is not None
        has_project = project is not None
        if has_folder == has_project:
            _tool_error("exactly one of 'folder' or 'project' is required")

        creation_options_used = (
            profile is not None
            or dpi is not None
            or dpi_fix is not None
            or project_file is not None
            or output_folder is not None
            or bool(color_mode_detect)
            or orientation_detect is not None
        )
        if has_project and creation_options_used:
            _tool_error("creation options cannot be used with 'project'")

        # types
        if last_step is not None and last_step not in LAST_STEPS:
            _tool_error(f"unknown last_step: {last_step}")
        if orientation_detect is not None:
            if not isinstance(orientation_detect, str) or orientation_detect not in (
                "pages",
                "book",
            ):
                _tool_error("orientation_detect must be 'pages' or 'book'")
        if threads is not None:
            if not _is_int(threads) or threads < 1:
                _tool_error("threads must be an integer >= 1")
        if export_formats is not None:
            if not isinstance(export_formats, list) or any(
                not isinstance(f, str) or f not in EXPORT_FORMATS for f in export_formats
            ):
                _tool_error("export_formats must be a list of: pdf, txt, hocr, alto")
        if dpi is not None:
            if not isinstance(dpi, str) or not parse_dpi_value(dpi):
                _tool_error(f"invalid dpi value: {dpi}")
        if dpi_fix is not None:
            if not isinstance(dpi_fix, str) or not parse_dpi_value(dpi_fix):
                _tool_error(f"invalid dpi_fix value: {dpi_fix}")

        roots = await allowed_roots(ctx, self.configured_roots)
        if not roots:
            _tool_error(
                "No allowed folders. Pass --allow-root <folder> when starting the server, "
                "or let the client provide roots."
            )

        # resolve source
        if has_folder:
            folder_resolved = _resolve_allowed(folder, roots)  # type: ignore[arg-type]
        else:
            if not isinstance(project, str) or not project.lower().endswith(".scantailor"):
                _tool_error("project must end with .ScanTailor")
            project_resolved = _resolve_allowed(project, roots)

        # resolve optional paths
        project_file_resolved = (
            _resolve_allowed(project_file, roots) if project_file is not None else None
        )
        output_folder_resolved = (
            _resolve_allowed(output_folder, roots) if output_folder is not None else None
        )
        export_folder_resolved = (
            _resolve_allowed(export_folder, roots) if export_folder is not None else None
        )

        profile_value: str | None = None
        if profile is not None:
            if not isinstance(profile, str) or not profile.strip():
                _tool_error("invalid profile name")
            if profile.lower().endswith(".stp"):
                profile_value = str(_resolve_allowed(profile, roots))
            else:
                if (
                    profile in (".", "..")
                    or any(ch in profile for ch in ("/", "\\", ":", "\x00"))
                    or profile.lower().endswith(".xml")
                ):
                    _tool_error(f"invalid profile name: {profile}")
                profile_value = profile

        # project key
        if has_project:
            project_path = project_resolved
        elif project_file_resolved is not None:
            project_path = project_file_resolved
        else:
            project_path = default_project_file(folder_resolved)
        project_key = normalize_path_key(project_path)

        # max runs / per-project
        running = [j for j in self.jobs.values() if j.state == "running"]
        if len(running) >= self.max_runs:
            ids = ", ".join(j.id for j in running)
            _tool_error(f"maximum number of concurrent runs reached ({self.max_runs}): {ids}")
        for j in running:
            if j.project_key == project_key:
                _tool_error(f"a run is already in progress for this project: {j.id}")

        # build args
        job_id = uuid.uuid4().hex
        report_dir = Path(tempfile.mkdtemp(prefix=f"scantailor-ocr-mcp-{job_id}-"))
        report_path = report_dir / f"{job_id}.json"

        args: list[str] = []
        if has_folder:
            args.append(str(folder_resolved))
            if profile_value is not None:
                args.append(f"--profile={profile_value}")
            if dpi is not None:
                args.append(f"--dpi={dpi}")
            if dpi_fix is not None:
                args.append(f"--dpi-fix={dpi_fix}")
            if project_file_resolved is not None:
                args.append(f"--project-file={project_file_resolved}")
            if output_folder_resolved is not None:
                args.append(f"--output-folder={output_folder_resolved}")
            if color_mode_detect:
                args.append("--color-mode-detect")
            if orientation_detect is not None:
                args.append(f"--orientation-detect={orientation_detect}")
        else:
            args.append(str(project_resolved))

        if last_step is not None:
            args.append(f"--last-step={last_step}")
        if threads is not None:
            args.append(f"--threads={threads}")
        if export_folder_resolved is not None:
            args.append(f"--export-folder={export_folder_resolved}")
        if export_name is not None:
            args.append(f"--export-name={export_name}")
        if export_formats:
            args.append("--export-formats=" + ",".join(export_formats))
        args.append("--progress-json")
        args.append(f"--report-file={report_path}")

        # ensure console once
        if not self._console_ready:
            self._console_ready = ensure_console()

        creationflags = 0
        if sys.platform == "win32":
            creationflags = subprocess.CREATE_NEW_PROCESS_GROUP

        job = Job(job_id, self.cli, project_key, report_dir, report_path)
        job.args = args
        # Registered before the first await, so a second call at the same time sees this run
        # in the checks above.
        self.jobs[job_id] = job
        try:
            job.process = await anyio.open_process(
                [str(self.cli), *args],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=creationflags,
            )
        except OSError as exc:
            del self.jobs[job_id]
            shutil.rmtree(report_dir, ignore_errors=True)
            _tool_error(f"Could not start CLI process: {exc}")
        except BaseException:
            del self.jobs[job_id]
            shutil.rmtree(report_dir, ignore_errors=True)
            raise
        if self._shutting_down:
            with contextlib.suppress(Exception):
                job.process.kill()
            with contextlib.suppress(Exception):
                await job.process.wait()
            with contextlib.suppress(Exception):
                await job.process.aclose()
            del self.jobs[job_id]
            shutil.rmtree(report_dir, ignore_errors=True)
            _tool_error("Server is shutting down")

        job.stdout_task = asyncio.create_task(job._read_stdout())
        job.stderr_task = asyncio.create_task(job._read_stderr())
        job.wait_task = asyncio.create_task(job._wait())

        logger.info("started run %s for %s", job_id, project_key)
        return job

    def get_job(self, job_id: str) -> Job:
        job = self.jobs.get(job_id)
        if job is None:
            _tool_error(f"unknown job_id: {job_id}")
        return job

    async def shutdown(self) -> None:
        self._shutting_down = True
        running = [j for j in self.jobs.values() if j.state == "running"]
        for job in running:
            job.cancel_requested = True
            if not job.cancel_signal_sent:
                job.cancel_signal_sent = True
                await job._send_cancel()
        # wait up to CANCEL_WAIT_TIMEOUT
        if running:
            try:
                await asyncio.wait_for(
                    asyncio.gather(*(j.finished.wait() for j in running), return_exceptions=True),
                    timeout=CANCEL_WAIT_TIMEOUT,
                )
            except TimeoutError:
                logger.warning("timed out waiting for runs to finish on shutdown")
        # kill leftovers
        for job in running:
            if not job.finished.is_set():
                logger.warning("killing run %s on shutdown", job.id)
                await job.kill()
                # A killed run leaves no report to read, and its folder would stay.
                shutil.rmtree(job.report_dir, ignore_errors=True)
