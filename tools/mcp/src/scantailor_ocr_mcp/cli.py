"""Finding and checking the ScanTailor OCR command line tool."""

from __future__ import annotations

import json
import logging
import os
import shutil
import subprocess
from pathlib import Path

logger = logging.getLogger(__name__)

ENV_VAR = "SCANTAILOR_OCR_CLI"
PROFILE_LIST_TIMEOUT = 30.0
RUN_TIMEOUT = 120.0


def find_cli() -> Path | None:
    """Find the CLI in the documented order: env, standard location, PATH.

    There is no single standard install location (the installers put the binary
    in the platform's usual bin directory, which differs by packager), so that
    step is intentionally skipped and reported in the project notes.
    """
    env = os.environ.get(ENV_VAR)
    if env:
        candidate = Path(env).expanduser()
        if candidate.is_file():
            return candidate.resolve()
        logger.warning("%s points to a missing file: %s", ENV_VAR, candidate)
    # Standard location: none unique -> skipped.
    found = shutil.which("scantailor-ocr-cli")
    if found:
        return Path(found).resolve()
    return None


def run_cli(args: list[str], timeout: float = RUN_TIMEOUT) -> tuple[int, str, str]:
    """Run the CLI once, without a shell, capturing stdout and stderr."""
    try:
        result = subprocess.run(
            args,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"CLI not found: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise TimeoutError(f"CLI timed out after {timeout}s") from exc
    return result.returncode, result.stdout or "", result.stderr or ""


def cli_version(cli: Path) -> str:
    try:
        rc, out, err = run_cli([str(cli), "--version"], timeout=10.0)
    except Exception as exc:  # pragma: no cover - best effort
        return f"<unreadable: {exc}>"
    text = (out or err).strip()
    return text or f"exit {rc}"


def is_capable(cli: Path) -> tuple[bool, str]:
    """Check whether the CLI supports what this server needs."""
    try:
        rc, out, err = run_cli([str(cli), "--profile-list"], timeout=PROFILE_LIST_TIMEOUT)
    except Exception as exc:
        return False, f"could not run {cli}: {exc}"
    if rc != 0:
        return False, f"{cli} --profile-list exited {rc}: {err.strip()}"
    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        return False, f"{cli} --profile-list did not produce JSON"
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        return False, f"{cli} --profile-list has an unexpected schema"
    return True, ""
