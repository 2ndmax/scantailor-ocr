"""Unit tests for job lifecycle, console handling, roots/path keys, and CLI error reporting."""

from __future__ import annotations

import asyncio
import contextlib
import ctypes
import json
import logging
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock

import anyio
import pytest
from mcp import Client
from mcp.server.mcpserver.exceptions import ToolError

from scantailor_ocr_mcp import cli as cli_mod
from scantailor_ocr_mcp import console as console_mod
from scantailor_ocr_mcp import jobs as jobs_mod
from scantailor_ocr_mcp import server as server_mod
from scantailor_ocr_mcp.jobs import Job, JobManager, parse_dpi_value
from scantailor_ocr_mcp.roots import normalize_path_key


class _FakeStream:
    def __init__(self, chunks: list[bytes] | None = None):
        self._chunks = list(chunks or [])

    async def receive(self, _n: int) -> bytes:
        if self._chunks:
            return self._chunks.pop(0)
        return b""


class _FakeProcess:
    def __init__(self, returncode: int = 0, stdout_chunks: list[bytes] | None = None):
        self.returncode = returncode
        self.stdout = _FakeStream(stdout_chunks)
        self.stderr = _FakeStream()
        self.killed = False
        self.waited = False
        self.closed = False

    def kill(self) -> None:
        self.killed = True

    async def wait(self) -> int:
        self.waited = True
        return self.returncode

    async def aclose(self) -> None:
        self.closed = True


def _ctx() -> SimpleNamespace:
    return SimpleNamespace(session=None)


def test_isolated_jobs_import_does_not_import_server() -> None:
    pkg_dir = Path(jobs_mod.__file__).resolve().parent
    code = (
        "import sys, types; "
        f"pkg = types.ModuleType('scantailor_ocr_mcp'); pkg.__path__ = [{str(pkg_dir)!r}]; "
        "sys.modules['scantailor_ocr_mcp'] = pkg; "
        "import scantailor_ocr_mcp.jobs as j; "
        "assert 'scantailor_ocr_mcp.server' not in sys.modules; "
        "assert hasattr(j, 'JobManager')"
    )
    proc = subprocess.run(
        [sys.executable, "-c", code],
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr


def test_normalize_path_key_case_handling(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("scantailor_ocr_mcp.roots.sys.platform", "win32")
    upper = tmp_path / "Book" / "BOOK.ScanTailor"
    lower = tmp_path / "book" / "book.scantailor"
    assert normalize_path_key(upper) == normalize_path_key(lower)


def test_concurrent_case_variant_project_key_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("scantailor_ocr_mcp.roots.sys.platform", "win32")
    allowed_dir = tmp_path.resolve()
    proj_upper = allowed_dir / "BOOK.ScanTailor"
    proj_upper.write_text("<project/>", encoding="utf-8")

    manager = JobManager(Path("cli"), True, "", [allowed_dir], max_runs=4)
    key = normalize_path_key(proj_upper)
    running = Job("existing-id", Path("cli"), key, tmp_path / "r1", tmp_path / "r1.json")
    running.state = "running"
    manager.jobs[running.id] = running

    async def main() -> None:
        with pytest.raises(ToolError, match="already in progress"):
            await manager.start_job(_ctx(), project=str(allowed_dir / "book.scantailor"))

    asyncio.run(main())


def test_ensure_console_preserves_64bit_hwnd_and_handles_failure(
    monkeypatch: pytest.MonkeyPatch, caplog: pytest.LogCaptureFixture
) -> None:
    monkeypatch.setattr(console_mod.sys, "platform", "win32")
    monkeypatch.setattr(console_mod.ensure_console, "__module__", console_mod.__name__)

    hwnd_64 = 0x1_0000_0001_2345
    show_calls: list[tuple[int, int]] = []

    kernel32 = MagicMock()
    kernel32.GetConsoleCP.return_value = 0
    kernel32.AllocConsole.return_value = 1
    kernel32.GetConsoleWindow.return_value = hwnd_64

    user32 = MagicMock()

    def _show(hwnd: int, cmd: int) -> int:
        show_calls.append((hwnd, cmd))
        return 1

    user32.ShowWindow.side_effect = _show

    def _windll(name: str, use_last_error: bool = False):
        assert use_last_error is True
        return kernel32 if name == "kernel32" else user32

    monkeypatch.setattr(ctypes, "WinDLL", _windll, raising=False)
    monkeypatch.setattr(
        ctypes, "wintypes", SimpleNamespace(UINT=int, BOOL=int, HWND=int), raising=False
    )

    assert console_mod.ensure_console() is True
    assert show_calls == [(hwnd_64, 0)]
    assert kernel32.GetConsoleWindow.restype is int
    assert user32.ShowWindow.argtypes == [int, ctypes.c_int]

    # AllocConsole failure logs warning and returns False
    kernel32.AllocConsole.return_value = 0
    monkeypatch.setattr(ctypes, "get_last_error", lambda: 5)
    caplog.set_level(logging.WARNING)
    assert console_mod.ensure_console() is False
    assert "AllocConsole failed with Win32 error 5" in caplog.text


def test_shutdown_race_during_open_process_kills_and_cleans_up(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    allowed_dir = tmp_path.resolve()
    book = allowed_dir / "book"
    book.mkdir()
    manager = JobManager(Path("cli"), True, "", [allowed_dir], max_runs=2)
    fake_proc = _FakeProcess()

    async def fake_open_process(*_args, **_kwargs):
        await manager.shutdown()
        return fake_proc

    monkeypatch.setattr(jobs_mod, "CANCEL_WAIT_TIMEOUT", 0.05)
    monkeypatch.setattr(jobs_mod, "ensure_console", lambda: True)
    monkeypatch.setattr(anyio, "open_process", fake_open_process)

    temp_root = tmp_path / "temps"
    temp_root.mkdir()
    monkeypatch.setattr("tempfile.tempdir", str(temp_root))

    async def main() -> None:
        with pytest.raises(ToolError, match="shutting down"):
            await manager.start_job(_ctx(), folder=str(book))

    asyncio.run(main())
    assert fake_proc.killed is True
    assert fake_proc.waited is True
    assert fake_proc.closed is True
    assert manager.jobs == {}
    assert list(temp_root.iterdir()) == []


def test_job_kill_waits_closes_and_sets_finished(tmp_path: Path) -> None:
    proc = _FakeProcess(returncode=-9)
    job = Job("j1", Path("cli"), "key", tmp_path / "rep", tmp_path / "rep" / "r.json")
    job.process = proc

    async def main() -> None:
        never_done = asyncio.create_task(asyncio.sleep(60))
        job.wait_task = never_done
        await job.kill()
        assert proc.killed is True
        assert proc.waited is True
        assert proc.closed is True
        assert job.state == "crashed"
        assert job.message
        assert job.finished.is_set()
        with contextlib.suppress(asyncio.CancelledError):
            await never_done

    asyncio.run(main())


def test_wait_sets_finished_and_crashed_when_finalize_raises(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    proc = _FakeProcess(returncode=0)
    job = Job("j2", Path("cli"), "key", tmp_path / "rep", tmp_path / "rep" / "r.json")
    job.process = proc

    async def boom() -> None:
        raise RuntimeError("boom in finalize")

    monkeypatch.setattr(job, "_finalize", boom)

    asyncio.run(job._wait())
    assert proc.waited is True
    assert proc.closed is True
    assert job.state == "crashed"
    assert "boom in finalize" in (job.message or "")
    assert job.finished.is_set()


def test_eviction_and_bounded_progress_events(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    allowed_dir = tmp_path.resolve()
    book = allowed_dir / "book"
    book.mkdir()
    manager = JobManager(Path("cli"), True, "", [allowed_dir], max_runs=2, max_finished_jobs=2)

    for idx in range(3):
        j = Job(f"old-{idx}", Path("cli"), f"k-{idx}", tmp_path / f"d{idx}", tmp_path / f"f{idx}")
        j.state = "succeeded"
        j.finished.set()
        manager.jobs[j.id] = j

    monkeypatch.setattr(jobs_mod, "ensure_console", lambda: True)

    lines = [
        (json.dumps({"event": "page", "step": "output", "done": i, "total": 1200}) + "\n").encode()
        for i in range(1200)
    ]
    fake_proc = _FakeProcess(returncode=2, stdout_chunks=lines)

    async def fake_open_process(*_args, **_kwargs):
        return fake_proc

    monkeypatch.setattr(anyio, "open_process", fake_open_process)

    async def main() -> None:
        job = await manager.start_job(_ctx(), folder=str(book))
        await job.finished.wait()
        assert "old-0" not in manager.jobs
        assert "old-1" in manager.jobs
        assert "old-2" in manager.jobs
        assert job.id in manager.jobs
        assert len(job.progress_events) == jobs_mod.MAX_PROGRESS_EVENTS
        assert job.progress_events[-1]["done"] == 1199
        assert job.state == "refused"
        assert job.message

    asyncio.run(main())


def test_parse_dpi_and_progress_non_numeric(tmp_path: Path) -> None:
    assert parse_dpi_value("300x300") is True
    assert parse_dpi_value("²") is False
    assert parse_dpi_value("²x300") is False

    job = Job("j3", Path("cli"), "key", tmp_path / "d", tmp_path / "d" / "r.json")
    job._update_progress({"event": "pass-start", "step": "deskew", "total": "abc"})
    assert job.step == "deskew"
    assert job.total == 0
    job._update_progress({"event": "page", "done": "nan", "total": True})
    assert job.done == 0
    assert job.total == 0


def test_is_capable_and_run_cli_error_classification(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    cli_exe = tmp_path / "cli"

    def raise_oserror(*_args, **_kwargs):
        raise OSError("missing dll")

    monkeypatch.setattr(cli_mod, "run_cli", raise_oserror)
    ok, msg = cli_mod.is_capable(cli_exe)
    assert ok is False
    assert "too old" not in msg.lower()
    assert "launch" in msg.lower()

    def raise_timeout(*_args, **_kwargs):
        raise subprocess.TimeoutExpired(["cli"], 10)

    monkeypatch.setattr(cli_mod, "run_cli", raise_timeout)
    ok, msg = cli_mod.is_capable(cli_exe)
    assert ok is False
    assert "too old" not in msg.lower()
    assert "timed out" in msg.lower()

    monkeypatch.setattr(cli_mod, "run_cli", lambda *_a, **_k: (1, "", "unknown option"))
    ok, msg = cli_mod.is_capable(cli_exe)
    assert ok is False
    assert "too old" in msg.lower()

    monkeypatch.setattr(cli_mod, "run_cli", lambda *_a, **_k: (139, "", "segfault"))
    ok, msg = cli_mod.is_capable(cli_exe)
    assert ok is False
    assert "too old" not in msg.lower()
    assert "139" in msg

    monkeypatch.setattr(cli_mod, "run_cli", lambda *_a, **_k: (0, "not-json", ""))
    ok, msg = cli_mod.is_capable(cli_exe)
    assert ok is False
    assert "too old" not in msg.lower()

    # _run_cli converts TimeoutExpired / OSError to ToolError
    monkeypatch.setattr(server_mod, "find_cli", lambda: cli_exe)
    monkeypatch.setattr(server_mod, "is_capable", lambda _c: (True, ""))
    monkeypatch.setattr(server_mod, "cli_version", lambda _c: "1.0")
    monkeypatch.setattr(server_mod, "run_cli", raise_timeout)
    srv, _ = server_mod.build_server([tmp_path.resolve()])

    async def main() -> None:
        async with Client(srv, raise_exceptions=False) as client:
            res = await client.call_tool("profile_list", {})
            assert res.is_error is True
            text = res.content[0].text
            assert "timed out" in text.lower()

    asyncio.run(main())


def test_project_mode_arg_rejection_and_terminal_states(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    allowed_dir = (tmp_path / "allowed").resolve()
    allowed_dir.mkdir()
    outside = (tmp_path / "outside").resolve()
    outside.mkdir()

    proj = allowed_dir / "book.ScanTailor"
    proj.write_text("<project/>", encoding="utf-8")
    book = allowed_dir / "book"
    book.mkdir()
    outside_stp = outside / "secret.stp"
    outside_stp.write_text("<profile/>", encoding="utf-8")

    manager = JobManager(Path("cli"), True, "", [allowed_dir], max_runs=4)

    async def check_rejections() -> None:
        for kwargs in (
            {"folder": str(book), "project_file": str(outside / "p.ScanTailor")},
            {"folder": str(book), "output_folder": str(outside / "out")},
            {"folder": str(book), "export_folder": str(outside / "exp")},
            {"folder": str(book), "profile": str(outside_stp)},
        ):
            with pytest.raises(ToolError, match="outside the allowed folders"):
                await manager.start_job(_ctx(), **kwargs)

    asyncio.run(check_rejections())

    # Terminal states: pages_failed (3), export_failed (4), crashed without report (0 / -11)
    for rc, write_report, expected_state in (
        (3, True, "pages_failed"),
        (4, True, "export_failed"),
        (0, False, "crashed"),
        (-11, False, "crashed"),
        (2, False, "refused"),
    ):
        rep_dir = tmp_path / f"rep_{rc}_{write_report}"
        rep_dir.mkdir()
        rep_path = rep_dir / "report.json"
        if write_report:
            rep_path.write_text(json.dumps({"schemaVersion": 1}), encoding="utf-8")
        job = Job(f"s-{rc}-{write_report}", Path("cli"), "k", rep_dir, rep_path)
        job.exit_code = rc
        asyncio.run(job._finalize())
        assert job.state == expected_state
        status = job.status()
        if expected_state in ("pages_failed", "export_failed"):
            assert status["report"]["schemaVersion"] == 1
        else:
            assert status["message"]

    # Successful start_job in project mode
    monkeypatch.setattr(jobs_mod, "ensure_console", lambda: True)
    fake_proc = _FakeProcess(returncode=2)

    async def fake_open_process(*_args, **_kwargs):
        return fake_proc

    monkeypatch.setattr(anyio, "open_process", fake_open_process)

    async def run_proj() -> None:
        job = await manager.start_job(_ctx(), project=str(proj))
        assert job.args[0] == str(proj)
        await job.finished.wait()
        assert job.state == "refused"
        assert job.message

    asyncio.run(run_proj())
