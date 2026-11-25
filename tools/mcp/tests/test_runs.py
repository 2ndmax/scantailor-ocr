"""Tests for run_start / run_status / run_cancel (stage 9)."""

from __future__ import annotations

import asyncio
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import pytest
from mcp.client.client import Client
from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from PIL import Image, ImageDraw

from scantailor_ocr_mcp.server import build_server

REPO_ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = Path(__file__).resolve().parent / "data"


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


def make_text_page(path: Path) -> None:
    """A page of 'text' bars rotated 90 degrees clockwise, so the detector
    returns 270 degrees for it. Drawn from makeTextPage in TestHelpers.h."""
    base = Image.new("RGB", (800, 2100), "white")
    d = ImageDraw.Draw(base)
    for line in range(45):
        y = 50 + line * 40
        for g in range(38):
            x = 60 + g * 18
            d.rectangle([x, y - 20, x + 2, y - 1], fill="black")
            if g % 2 == 0:
                d.rectangle([x, y - 32, x + 2, y - 21], fill="black")
            if g % 17 == 0:
                d.rectangle([x - 2, y - 12, x + 5, y - 10], fill="black")
    out = base.transpose(Image.ROTATE_270)
    out.save(path, dpi=(300, 300))


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
            # The report goes to the folder of the server, never to a folder of the user.
            assert not list(allowed.rglob("*.json"))

    asyncio.run(main())


# 2. progress (running status collected)
def test_run_progress(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 2)

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start", {"folder": str(book), "profile": "Default", "last_step": "output"}
            )
            job_id = r.structured_content["job_id"]
            sc, running = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            # A short run may end before a status call sees it running, so the progress lines the
            # job took from the CLI are checked instead: what run_status reports while running.
            job = state["job_manager"].jobs[job_id]
            steps = [e["step"] for e in job.progress_events if e.get("event") == "pass-start"]
            assert steps == ["split-pages", "margins", "output"]
            output_pages = [
                e
                for e in job.progress_events
                if e.get("event") == "page" and e.get("step") == "output"
            ]
            assert [e["done"] for e in output_pages] == [1, 2]
            assert all(e["total"] == 2 for e in output_pages)
            # And what a status call saw while running had all three fields.
            assert all(
                rs["step"] is not None and rs["total"] >= rs["done"] for rs in running if rs["step"]
            )

    asyncio.run(main())


# 3. export
def test_run_export(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)
    export_dir = allowed / "export"
    export_dir.mkdir()
    # The built-in profiles recognize German and English text, and the CLI refuses an export
    # whose OCR languages aren't installed, as on the CI runners. This profile turns text
    # recognition off, so the test doesn't depend on the languages of the machine. It is the
    # Default profile with the PDF text layer off and no languages, as written by
    # DefaultParamsProfileManager; profiles of older versions stay readable. The profile is a
    # path, so it has to lie in an allowed folder too.
    profile = allowed / "no-ocr.stp"
    shutil.copyfile(DATA_DIR / "no-ocr.stp", profile)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": str(profile),
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
            # The CLI writes the project of a folder after processing; it exists only if the
            # cancel let the CLI end normally.
            assert (book / "book.ScanTailor").is_file()
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
        assert (book / "book.ScanTailor").is_file()
        assert len(list((book / "out").glob("*.tif"))) < 18

    asyncio.run(main())


# 10. server ends during run
def test_server_shutdown_during_run(cli, tmp_path: Path):
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
                # The client leaves while the Output step is under way.
                deadline = time.time() + 120
                while not list((book / "out").glob("*.tif")):
                    assert time.time() < deadline, "no output file before leaving"
                    await asyncio.sleep(0.2)
        # Leaving made the server cancel the run, not kill it, and not wait for its end: the
        # project of the folder is written only when the CLI ends normally, and pages are left.
        assert (book / "book.ScanTailor").is_file()
        assert len(list((book / "out").glob("*.tif"))) < 18

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


# A client may end the server without letting it clean up, so starting it must not leave a folder.
def test_starting_the_server_creates_no_folder(cli, tmp_path: Path, monkeypatch):
    temp = tmp_path / "temp"
    temp.mkdir()
    monkeypatch.setattr(tempfile, "tempdir", str(temp))

    build_server([tmp_path])

    assert list(temp.iterdir()) == []


# The report is read when the run ends, so its folder goes then, not only when the server ends.
def test_report_folder_goes_when_the_run_ends(cli, tmp_path: Path, monkeypatch):
    temp = tmp_path / "temp"
    temp.mkdir()
    monkeypatch.setattr(tempfile, "tempdir", str(temp))
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start", {"folder": str(book), "profile": "Default", "last_step": "output"}
            )
            final, _ = await wait_finished(client, r.structured_content["job_id"])
            assert final["state"] == "succeeded"
            assert final["report"] is not None
            assert list(temp.iterdir()) == []

    asyncio.run(main())


# orientation detection: pages
def test_run_orientation_detect_pages(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    make_text_page(book / "text1.png")
    make_text_page(book / "text2.png")
    empty = Image.new("RGB", (2100, 800), "white")
    empty.save(book / "empty.png", dpi=(300, 300))

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": "Default",
                    "last_step": "output",
                    "orientation_detect": "pages",
                },
            )
            assert r.is_error is False
            job_id = r.structured_content["job_id"]
            sc, _ = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            assert sc["exit_code"] == 0
            pages = sc["report"]["pages"]
            by_name = {Path(p["file"]).name: p for p in pages}
            assert by_name["text1.png"]["rotation"] == 270
            assert by_name["text1.png"]["rotationDetection"] == "detected"
            assert by_name["text2.png"]["rotation"] == 270
            assert by_name["text2.png"]["rotationDetection"] == "detected"
            assert by_name["empty.png"]["rotationDetection"] == "undecided"

    asyncio.run(main())


# orientation detection: book
def test_run_orientation_detect_book(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    for i in range(5):
        make_text_page(book / f"text{i}.png")
    empty = Image.new("RGB", (2100, 800), "white")
    empty.save(book / "empty.png", dpi=(300, 300))

    async def main():
        server, _ = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": "Default",
                    "last_step": "output",
                    "orientation_detect": "book",
                },
            )
            assert r.is_error is False
            job_id = r.structured_content["job_id"]
            sc, _ = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            pages = sc["report"]["pages"]
            by_name = {Path(p["file"]).name: p for p in pages}
            assert by_name["empty.png"]["rotation"] == 270
            assert by_name["empty.png"]["rotationDetection"] == "book-rule"

    asyncio.run(main())


# orientation detection: tool errors
def test_run_orientation_detect_tool_errors(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    make_page(book / "page1.png")
    proj = book / "book.ScanTailor"
    subprocess.run(
        [str(cli_path()), str(book), "--profile=Default", "--last-step=fix-orientation"],
        check=True,
        capture_output=True,
    )

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            # unknown value: refused by the server, naming the allowed values
            r = await client.call_tool(
                "run_start", {"folder": str(book), "orientation_detect": "nope"}
            )
            assert r.is_error is True
            assert "'pages' or 'book'" in r.content[0].text
            # orientation_detect with project
            r = await client.call_tool(
                "run_start", {"project": str(proj), "orientation_detect": "pages"}
            )
            assert r.is_error is True
            # neither call started a run
            assert state["job_manager"].jobs == {}

    asyncio.run(main())


# orientation detection: arguments
def test_run_orientation_detect_args(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    make_page(book / "page1.png")

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": "Default",
                    "last_step": "output",
                    "orientation_detect": "book",
                },
            )
            assert r.is_error is False
            job_id = r.structured_content["job_id"]
            job = state["job_manager"].jobs[job_id]
            assert "--orientation-detect=book" in job.args
            await wait_finished(client, job_id)

            # without the parameter
            book2 = allowed / "book2"
            book2.mkdir()
            make_page(book2 / "page1.png")
            r2 = await client.call_tool(
                "run_start", {"folder": str(book2), "profile": "Default", "last_step": "output"}
            )
            assert r2.is_error is False
            job_id2 = r2.structured_content["job_id"]
            job2 = state["job_manager"].jobs[job_id2]
            assert not any(a.startswith("--orientation-detect") for a in job2.args)
            await wait_finished(client, job_id2)

    asyncio.run(main())


# orientation detection: progress
def test_run_orientation_detect_progress(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    make_text_page(book / "text1.png")
    make_text_page(book / "text2.png")
    empty = Image.new("RGB", (2100, 800), "white")
    empty.save(book / "empty.png", dpi=(300, 300))

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            r = await client.call_tool(
                "run_start",
                {
                    "folder": str(book),
                    "profile": "Default",
                    "last_step": "output",
                    "orientation_detect": "pages",
                },
            )
            assert r.is_error is False
            job_id = r.structured_content["job_id"]
            sc, running = await wait_finished(client, job_id)
            assert sc["state"] == "succeeded"
            job = state["job_manager"].jobs[job_id]
            steps = [e["step"] for e in job.progress_events if e.get("event") == "pass-start"]
            assert steps and steps[0] == "orientation-detect"
            od_pages = [
                e
                for e in job.progress_events
                if e.get("event") == "page" and e.get("step") == "orientation-detect"
            ]
            statuses = {e.get("status") for e in od_pages}
            assert "ok" in statuses
            assert "uncertain" in statuses
            ok_events = [e for e in od_pages if e.get("status") == "ok"]
            assert all(e.get("rotation") == 270 for e in ok_events)
            assert all(e.get("rotationDetection") == "detected" for e in ok_events)

    asyncio.run(main())


def test_profile_path_traversal_rejected(cli, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = make_book(allowed / "book", 1)
    outside_profile = tmp_path / "secret.stp"
    shutil.copyfile(DATA_DIR / "no-ocr.stp", outside_profile)

    async def main():
        server, state = build_server([allowed])
        async with Client(server) as client:
            bad_profiles = [
                str(tmp_path / "secret"),
                "../../secret",
                "sub/secret",
                "sub\\secret",
                "..",
                ".",
                "C:secret",
                "",
                str(outside_profile),
            ]
            for profile in bad_profiles:
                r = await client.call_tool(
                    "run_start",
                    {"folder": str(book), "profile": profile, "last_step": "output"},
                )
                assert r.is_error is True, f"profile {profile!r} should have been rejected"
            assert state["job_manager"].jobs == {}

    asyncio.run(main())
