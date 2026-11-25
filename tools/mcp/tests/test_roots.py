"""Tests for roots and file URI resolution."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path
from types import SimpleNamespace

from scantailor_ocr_mcp.roots import client_roots, file_uri_to_path


def test_file_uri_to_path_rejects_empty_and_invalid_uris(tmp_path: Path):
    assert file_uri_to_path("file://") is None
    assert file_uri_to_path("file://localhost") is None
    assert file_uri_to_path("http://example.com/books") is None
    assert file_uri_to_path("file://server") is None
    assert file_uri_to_path("file://server/") is None
    assert file_uri_to_path(tmp_path.as_uri()) == tmp_path

    if sys.platform == "win32":
        assert file_uri_to_path("file:///") is None
        assert file_uri_to_path("file://localhost/") is None
        assert file_uri_to_path("file:///C:") is None
        assert file_uri_to_path("file:///C:/") == Path("C:/")
        assert file_uri_to_path("file://localhost/C:/Books") == Path("C:/Books")
        assert file_uri_to_path("file://C:/Books") is None
        assert file_uri_to_path("file://server/share/books") == Path("\\\\server\\share\\books")
    else:
        assert file_uri_to_path("file://server/share/books") is None
        assert file_uri_to_path("file:///") == Path("/")


def test_client_roots_never_resolves_bad_uris_to_cwd(tmp_path: Path):
    allowed_dir = tmp_path / "books"
    allowed_dir.mkdir()
    bad_and_good = [
        SimpleNamespace(uri="file://"),
        SimpleNamespace(uri="file:///"),
        SimpleNamespace(uri="file://localhost"),
        SimpleNamespace(uri="file://server/share/books"),
        SimpleNamespace(uri=allowed_dir.as_uri()),
    ]

    async def _list_roots():
        return SimpleNamespace(roots=bad_and_good)

    ctx = SimpleNamespace(session=SimpleNamespace(list_roots=_list_roots))
    roots = asyncio.run(client_roots(ctx))

    assert Path.cwd().resolve() not in roots
    assert allowed_dir.resolve() in roots
    assert all(r.is_absolute() for r in roots)
