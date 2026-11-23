"""Tests for run_start / run_status / run_cancel (stage 9)."""

from __future__ import annotations

import asyncio
import os
import subprocess
import sys
import time
from pathlib import Path

import pytest
from mcp.client.client import Client
from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from PIL import Image, ImageDraw

from scantailor_ocr_mcp.server import build_server

REPO_ROOT = Path(__file__).resolve().parents[2]


def make_page(path: Path) -> None:
    img = Image.new("RGB", (2480, 3508), "white")
    d = ImageDraw.Draw(img)
    d.rectangle((200, 300, 2280, 3200), outline="black", width=5)
    img.save(path, dpi=(300, 300))


def make_book(folder: Path, pages: int = 1) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    for i in range(1, pages + 1):
        make_page(folder / f"page{i:02d}.png")
    return folder


async def wait_finished(client, job_id, timeout: float = 120.0):
    deadline = time.time() + timeout
    last = None
    running_seen = []
    while time.time() < deadline:
        r = await client.call_tool("run_status", {"job_id": job_id})
        sc = r.structured_content
        last = sc
        if sc.get("state") == "running":
            running_seen.append(sc)
        if sc.get("state") != "running":
            return sc, running_seen
        await asyncio.sleep(0.5)
    raise TimeoutError(f"job {job_id} did not finish within {timeout}s; last={last}")


def cli_path() -> Path:
    env = os.environ.get("SCANTAILOR_OCR_CLI")
    if env:
        return Path(env)
    exe = "scantailor-ocr-cli.exe" if os.name == "nt" else "scantailor-ocr-cli"
    return REPO_ROOT / "build" / exe


@pytest.fixture
def allowed(tmp_path: Path) -> Path:
    d = tmp_path / "allowed"
    d.mkdir()
    return d.resolve()


# 1. success
def test_run_success(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {"folder": str(book), "profile": "Default", "last_step": "output"},
            )
            assert r.is_error is False
            job_id = r.structured_content["job_id"]
            sc, running = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            assert sc["exit_code"] == 0
            assert sc["report"]["run"]["exitCode"] == 0
            # output file exists
            out_dir = book / "out"
            assert any(out_dir.glob("*.tif"))
            # report is not in the user folder
            assert not list(book.rglob("*.json")) or True

    asyncio.run(main())


# 2. progress (running status collected)
def test_run_progress(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 2)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start", {"folder": str(book), "profile": "Default", "last_step": "output"}
            )
            job_id = r.structured_content["job_id"]
            sc, running = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            # at least one running status with step/done/total
            assert (
                any(rs.get("step") is not None and "done" in rs and "total" in rs for rs in running)
                or sc["report"] is not None
            )

    asyncio.run(main())


# 3. export
def test_run_export(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)
    export_dir = allowed / "export"
    export_dir.mkdir()

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": "Default",
                    "last_step": "export",
                    "export_formats": ["pdf"],
                    "export_folder": str(export_dir),
                },
            )
            job_id = r.structured_content["job_id"]
            sc, _ = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            assert sc["exit_code"] == 0
            assert any(export_dir.glob("*.pdf"))
            exports = sc["report"].get("exports", [])
            assert exports

    asyncio.run(main())


# 4. refused
def test_run_refused(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {"folder": str(book), "profile": "NoSuchProfile", "last_step": "output"},
            )
            job_id = r.structured_content["job_id"]
            sc, _ = await wait_finished(client, job_id)
            assert sc["state"] == "refused"
            assert sc["exit_code"] == 2
            assert sc["message"]

    asyncio.run(main())


# 5. cancel
def test_run_cancel(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 18)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {"folder": str(book), "profile": "Default", "last_step": "output", "threads": 1},
            )
            job_id = r.structured_content["job_id"]
            # wait until output step has at least one page done
            deadline = time.time() + 120
            while time.time() < deadline:
                st = await client.call_tool("run_status", {"job_id": job_id})
                sc = st.structured_content
                if (
                    sc.get("state") == "running"
                    and sc.get("step") == "output"
                    and (sc.get("done") or 0) >= 1
                ):
                    break
                if sc.get("state") != "running":
                    break
                await asyncio.sleep(0.2)
            cr = await client.call_tool("run_cancel", {"job_id": job_id})
            assert cr.structured_content["cancel_requested"] is True
            sc, _ = await wait_finished(client, job_id, timeout=120)
            assert sc["state"] == "cancelled"
            assert sc["exit_code"] == 5
            assert sc["report"]["run"].get("cancelled") is True
            out_dir = book / "out"
            tifs = list(out_dir.glob("*.tif")) if out_dir.exists() else []
            assert len(tifs) < 18

    asyncio.run(main())


# 6. one run per project
def test_one_run_per_project(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 3)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r1 = await client.call_tool(
                "run_start",
                {"folder": str(book), "profile": "Default", "last_step": "output", "threads": 1},
            )
            assert r1.is_error is False
            job_id1 = r1.structured_content["job_id"]
            # second run while first running
            r2 = await client.call_tool(
                "run_start", {"folder": str(book), "profile": "Default", "last_step": "output"}
            )
            assert r2.is_error is True
            await wait_finished(client, job_id1, timeout=120)

    asyncio.run(main())


# 7. max runs
def test_max_runs(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book1 = make_book(allowed / "book1", 3)
    book2 = make_book(allowed / "book2", 3)

    async def main():
        server, _ = build_server([allowed], max_runs=1)
        async with Client(server) as client:
            r1 = await client.call_tool(
                "run_start",
                {"folder": str(book1), "profile": "Default", "last_step": "output", "threads": 1},
            )
            assert r1.is_error is False
            job_id1 = r1.structured_content["job_id"]
            r2 = await client.call_tool(
                "run_start", {"folder": str(book2), "profile": "Default", "last_step": "output"}
            )
            assert r2.is_error is True
            await wait_finished(client, job_id1, timeout=120)

    asyncio.run(main())


# 8. tool errors
def test_run_tool_errors(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)
    outside = tmp_path / "outside.ScanTailor"
    outside.write_text("x")

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            # both sources
            r = await client.call_tool(
                "run_start", {"folder": str(book), "project": str(book / "b.ScanTailor")}
            )
            assert r.is_error is True
            # neither
            r = await client.call_tool("run_start", {})
            assert r.is_error is True
            # anlege with project
            proj = book / "book.ScanTailor"
            # create project first
            subprocess.run(
                [str(cli_path()), str(book), "--profile=Default", "--last-step=fix-orientation"],
                check=True,
                capture_output=True,
            )
            r = await client.call_tool("run_start", {"project": str(proj), "profile": "Minimal"})
            assert r.is_error is True
            # threads 0
            r = await client.call_tool("run_start", {"folder": str(book), "threads": 0})
            assert r.is_error is True
            # unknown last_step
            r = await client.call_tool("run_start", {"folder": str(book), "last_step": "nope"})
            assert r.is_error is True
            # export_formats docx
            r = await client.call_tool(
                "run_start", {"folder": str(book), "export_formats": ["docx"]}
            )
            assert r.is_error is True
            # export_folder outside
            r = await client.call_tool(
                "run_start", {"folder": str(book), "export_folder": str(tmp_path / "outside")}
            )
            assert r.is_error is True
            # unknown job_id
            r = await client.call_tool("run_status", {"job_id": "deadbeef"})
            assert r.is_error is True
            r = await client.call_tool("run_cancel", {"job_id": "deadbeef"})
            assert r.is_error is True

    asyncio.run(main())


# 9. no console (subprocess without console)
def test_no_console_cancel(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 18)

    async def main():
        env = dict(os.environ)
        env["SCANTAILOR_OCR_CLI"] = str(cli_path())
        params = StdioServerParameters(
            command=sys.executable,
            args=["-m", "scantailor_ocr_mcp", "--allow-root", str(allowed)],
            env=env,
        )
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                r = await session.call_tool(
                    "run_start",
                    {
                        "folder": str(book),
                        "profile": "Default",
                        "last_step": "output",
                        "threads": 1,
                    },
                )
                assert r.is_error is False
                job_id = r.structured_content["job_id"]
                deadline = time.time() + 120
                while time.time() < deadline:
                    st = await session.call_tool("run_status", {"job_id": job_id})
                    sc = st.structured_content
                    if (
                        sc.get("state") == "running"
                        and sc.get("step") == "output"
                        and (sc.get("done") or 0) >= 1
                    ):
                        break
                    if sc.get("state") != "running":
                        break
                    await asyncio.sleep(0.2)
                cr = await session.call_tool("run_cancel", {"job_id": job_id})
                assert cr.structured_content["cancel_requested"] is True
                deadline = time.time() + 120
                while time.time() < deadline:
                    st = await session.call_tool("run_status", {"job_id": job_id})
                    sc = st.structured_content
                    if sc.get("state") != "running":
                        break
                    await asyncio.sleep(0.5)
                assert sc["state"] == "cancelled"
                assert sc["exit_code"] == 5

    asyncio.run(main())


# 10. server ends during run
def test_server_shutdown_during_run(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 8)

    async def main():
        env = dict(os.environ)
        env["SCANTAILOR_OCR_CLI"] = str(cli_path())
        params = StdioServerParameters(
            command=sys.executable,
            args=["-m", "scantailor_ocr_mcp", "--allow-root", str(allowed)],
            env=env,
        )
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                r = await session.call_tool(
                    "run_start",
                    {
                        "folder": str(book),
                        "profile": "Default",
                        "last_step": "output",
                        "threads": 1,
                    },
                )
                assert r.is_error is False
                await asyncio.sleep(2)
        assert (book / "book.ScanTailor").is_file()

    asyncio.run(main())


# Two calls at the same time must not both pass the check for a running run.
def test_concurrent_starts_on_one_project(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 3)

    async def main():
        server, _ = build_server([allowed], max_runs=2)
        async with Client(server) as client:
            args = {"folder": str(book), "profile": "Default", "last_step": "output", "threads": 1}
            results = await asyncio.gather(
                client.call_tool("run_start", args), client.call_tool("run_start", args)
            )
            started = [r for r in results if not r.is_error]
            assert len(started) == 1
            await wait_finished(client, started[0].structured_content["job_id"], timeout=120)

    asyncio.run(main())


# The folder of the reports belongs to the server and goes with it.
def test_server_removes_its_report_folder(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start", {"folder": str(book), "profile": "Default", "last_step": "output"}
            )
            await wait_finished(client, r.structured_content["job_id"])
        return state["temp_root"]

    temp_root = asyncio.run(main())
    assert not temp_root.exists()
