"""Shared test fixtures."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]


def find_cli() -> Path:
    env = os.environ.get("SCANTAILOR_OCR_CLI")
    if env:
        p = Path(env)
        if not p.is_file():
            raise RuntimeError(f"SCANTAILOR_OCR_CLI points to a missing file: {p}")
        return p.resolve()
    exe = "scantailor-ocr-cli.exe" if os.name == "nt" else "scantailor-ocr-cli"
    candidate = REPO_ROOT / "build" / exe
    if not candidate.is_file():
        raise RuntimeError(
            "scantailor-ocr-cli not found. Build it first or set SCANTAILOR_OCR_CLI."
        )
    return candidate.resolve()


@pytest.fixture(scope="session")
def cli() -> Path:
    return find_cli()


@pytest.fixture(autouse=True, scope="session")
def _set_cli_env(cli: Path):
    os.environ["SCANTAILOR_OCR_CLI"] = str(cli)
    yield
    os.environ.pop("SCANTAILOR_OCR_CLI", None)


@pytest.fixture
def allowed(tmp_path: Path) -> Path:
    d = tmp_path / "allowed"
    d.mkdir()
    return d.resolve()
