"""Tests for the MCP server (stage 8)."""

from __future__ import annotations

import asyncio
import os
import subprocess
from pathlib import Path

import pytest
from mcp.client.client import Client
from PIL import Image, ImageDraw

from scantailor_ocr_mcp.server import build_server

REPO_ROOT = Path(__file__).resolve().parents[2]


def _cli() -> Path:
    return Path(os.environ["SCANTAILOR_OCR_CLI"]) if "SCANTAILOR_OCR_CLI" in os.environ else None  # type: ignore


def run_async(coro):
    return asyncio.run(coro)


def direct_cli(cli: Path, *args: str) -> tuple[int, str, str]:
    # The CLI writes UTF-8, whatever the code page of the console.
    r = subprocess.run([str(cli), *args], capture_output=True, encoding="utf-8")
    return r.returncode, r.stdout, r.stderr


def make_page(path: Path) -> None:
    img = Image.new("RGB", (2480, 3508), "white")
    d = ImageDraw.Draw(img)
    d.rectangle((200, 300, 2280, 3200), outline="black", width=5)
    img.save(path, dpi=(300, 300))


def make_project(tmp_path: Path, cli: Path) -> tuple[Path, Path]:
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    book = allowed / "book"
    book.mkdir()
    make_page(book / "page1.png")
    make_page(book / "page2.png")
    subprocess.run(
        [str(cli), str(book), "--profile=Default", "--last-step=fix-orientation"],
        check=True,
        capture_output=True,
        text=True,
    )
    project = book / "book.ScanTailor"
    assert project.is_file()
    return project, allowed


def test_list_tools(cli: Path, allowed: Path):
    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            tools = await client.list_tools()
            names = [t.name for t in tools.tools]
            assert names == ["profile_list", "project_info"]
            for t in tools.tools:
                assert t.input_schema is not None

    run_async(main())


def test_profile_list(cli: Path, allowed: Path):
    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("profile_list", {})
            assert r.is_error is False
            sc = r.structured_content
            assert sc["state"] == "succeeded"
            assert sc["exit_code"] == 0
            assert "Default" in [p["name"] for p in sc["profiles"]["profiles"]]
            assert "Minimal" in [p["name"] for p in sc["profiles"]["profiles"]]
            for p in sc["profiles"]["profiles"]:
                if p["name"] in ("Default", "Minimal"):
                    assert p["source"] == "built-in"
            # compare with direct CLI call
            rc, out, err = direct_cli(cli, "--profile-list")
            assert rc == 0
            import json

            assert sc["profiles"] == json.loads(out)

    run_async(main())


def test_project_info(cli: Path, tmp_path: Path):
    project, allowed = make_project(tmp_path, cli)

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(project)})
            assert r.is_error is False
            sc = r.structured_content
            assert sc["state"] == "succeeded"
            assert sc["exit_code"] == 0
            assert len(sc["project"]["pages"]) == 2
            assert sc["project"]["projectFile"] == str(project)
            rc, out, err = direct_cli(cli, "--project-info", str(project))
            assert rc == 0
            import json

            assert sc["project"] == json.loads(out)

    run_async(main())


def test_project_info_keeps_non_ascii_paths(cli: Path, tmp_path: Path):
    """Folder and file names outside ASCII come back as they are, not in another code page."""
    allowed = tmp_path / "Bücher"
    book = allowed / "Straße"
    book.mkdir(parents=True)
    make_page(book / "Seite.png")
    subprocess.run(
        [str(cli), str(book), "--profile=Default", "--last-step=fix-orientation"],
        check=True,
        capture_output=True,
    )
    project = book / "Straße.ScanTailor"
    assert project.is_file()

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(project)})
            assert r.is_error is False
            sc = r.structured_content
            assert sc["project"]["projectFile"] == str(project)
            assert sc["project"]["pages"][0]["file"] == str(book / "Seite.png")

    run_async(main())


def test_project_info_creates_nothing(cli: Path, tmp_path: Path):
    project, allowed = make_project(tmp_path, cli)
    before = sorted(str(p.relative_to(allowed)) for p in allowed.rglob("*"))

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(project)})
            assert r.is_error is False

    run_async(main())
    after = sorted(str(p.relative_to(allowed)) for p in allowed.rglob("*"))
    assert before == after


def test_project_info_refused(cli: Path, tmp_path: Path):
    bad = tmp_path / "allowed" / "bad.ScanTailor"
    allowed = tmp_path / "allowed"
    allowed.mkdir(exist_ok=True)
    bad.write_text("not a project")

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(bad)})
            assert r.is_error is False
            sc = r.structured_content
            assert sc["state"] == "refused"
            assert sc["exit_code"] == 2
            assert sc["message"]

    run_async(main())


def test_tool_errors(cli: Path, tmp_path: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    outside = tmp_path / "outside.ScanTailor"
    outside.write_text("x")
    # sibling with same prefix
    sibling = tmp_path / "allowed2"
    sibling.mkdir()
    sibling_project = sibling / "p.ScanTailor"
    sibling_project.write_text("x")
    # path via ..
    via_dotdot = allowed / "sub" / ".." / ".." / "outside.ScanTailor"

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            # outside
            r = await client.call_tool("project_info", {"project": str(outside)})
            assert r.is_error is True
            # sibling
            r = await client.call_tool("project_info", {"project": str(sibling_project)})
            assert r.is_error is True
            # via ..
            r = await client.call_tool("project_info", {"project": str(via_dotdot)})
            assert r.is_error is True
            # no .ScanTailor
            r = await client.call_tool("project_info", {"project": str(allowed / "foo.txt")})
            assert r.is_error is True

    run_async(main())

    # no allowed roots
    async def main2():
        server, _ = build_server([])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(outside)})
            assert r.is_error is True

    run_async(main2())


def test_cli_not_found(tmp_path: Path):
    old = os.environ.get("SCANTAILOR_OCR_CLI")
    os.environ["SCANTAILOR_OCR_CLI"] = str(tmp_path / "nope.exe")
    try:

        async def main():
            server, _ = build_server([])
            async with Client(server, raise_exceptions=False) as client:
                r = await client.call_tool("profile_list", {})
                assert r.is_error is True
                # message surfaces in content text
                text = r.content[0].text
                assert "not found" in text.lower() or "too old" in text.lower()

        run_async(main())
    finally:
        if old is None:
            os.environ.pop("SCANTAILOR_OCR_CLI", None)
        else:
            os.environ["SCANTAILOR_OCR_CLI"] = old


def test_cli_too_old(tmp_path: Path):
    bat = tmp_path / "fake_cli.cmd"
    bat.write_text('@echo off\nif "%~1"=="--profile-list" exit /b 1\nexit /b 0\n')
    old = os.environ.get("SCANTAILOR_OCR_CLI")
    os.environ["SCANTAILOR_OCR_CLI"] = str(bat)
    try:

        async def main():
            server, _ = build_server([])
            async with Client(server, raise_exceptions=False) as client:
                r = await client.call_tool("profile_list", {})
                assert r.is_error is True
                text = r.content[0].text
                assert "too old" in text.lower()

        run_async(main())
    finally:
        if old is None:
            os.environ.pop("SCANTAILOR_OCR_CLI", None)
        else:
            os.environ["SCANTAILOR_OCR_CLI"] = old


def test_symlink(tmp_path: Path, cli: Path):
    allowed = tmp_path / "allowed"
    allowed.mkdir()
    outside = tmp_path / "outside"
    outside.mkdir()
    link = allowed / "escape"
    try:
        os.symlink(outside, link, target_is_directory=True)
    except OSError as exc:
        pytest.skip(f"symlink not available: {exc}")
    project = outside / "p.ScanTailor"
    project.write_text("x")
    linked_project = link / "p.ScanTailor"

    async def main():
        server, _ = build_server([allowed])
        async with Client(server, raise_exceptions=False) as client:
            r = await client.call_tool("project_info", {"project": str(linked_project)})
            assert r.is_error is True

    run_async(main())
