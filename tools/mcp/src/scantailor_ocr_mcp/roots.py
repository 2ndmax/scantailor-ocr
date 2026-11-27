"""Resolving and checking allowed folders."""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse

logger = logging.getLogger(__name__)


def resolve_path(path: str | Path) -> Path:
    return Path(path).expanduser().resolve()


def _norm(p: Path) -> str:
    s = str(p)
    if sys.platform == "win32":
        s = s.casefold()
    return s


def _parts(p: Path) -> tuple[str, ...]:
    """The components of the path for comparing, without case on Windows."""
    if sys.platform == "win32":
        return tuple(part.casefold() for part in p.parts)
    return p.parts


def is_allowed(path: str | Path, allowed: list[Path]) -> bool:
    """True if the resolved path equals an allowed folder or lies inside it.

    Compared by path components, not by text: "C:\\Books2" is not inside "C:\\Books", and a drive
    root or "/" contains everything on it.
    """
    try:
        target = _parts(resolve_path(path))
    except (OSError, RuntimeError):
        return False
    for root in allowed:
        try:
            base = _parts(root.resolve())
        except (OSError, RuntimeError):
            continue
        if target[: len(base)] == base:
            return True
    return False


def normalize_path_key(path: Path) -> str:
    """Return a normalized comparison key for *path*."""
    return _norm(resolve_path(path))


def resolve_allowed_path(path: str | Path, allowed: list[Path]) -> Path:
    """Resolve *path* and ensure it is within *allowed*, or raise ValueError."""
    try:
        resolved = resolve_path(path)
    except (OSError, RuntimeError) as exc:
        raise ValueError(f"Could not resolve path: {exc}") from exc
    if not is_allowed(resolved, allowed):
        raise ValueError(f"Path is outside the allowed folders: {resolved}")
    return resolved


def file_uri_to_path(uri: str) -> Path | None:
    parsed = urlparse(uri)
    if parsed.scheme != "file":
        return None
    netloc = unquote(parsed.netloc)
    raw_path = unquote(parsed.path)
    if "\x00" in netloc or "\x00" in raw_path:
        return None

    if sys.platform == "win32":
        if netloc and netloc.lower() != "localhost":
            if any(ch in netloc for ch in ("/", "\\", ":")):
                return None
            if not raw_path.startswith("/") or not any(seg for seg in raw_path.split("/")):
                return None
            candidate = Path("\\\\" + netloc + raw_path.replace("/", "\\"))
        else:
            if (
                len(raw_path) >= 3
                and raw_path[0] == "/"
                and raw_path[1].isalpha()
                and raw_path[2] == ":"
            ):
                raw_path = raw_path[1:]
            if not raw_path:
                return None
            candidate = Path(raw_path)
    else:
        if netloc and netloc.lower() != "localhost":
            return None
        if not raw_path.startswith("/"):
            return None
        candidate = Path(raw_path)

    return candidate if candidate.is_absolute() else None


async def client_roots(ctx) -> list[Path]:
    """Best-effort roots from the client session."""
    result: list[Path] = []
    try:
        lr = await ctx.session.list_roots()
    except Exception as exc:
        # A client without roots is fine; the log says so, as it decides what is allowed.
        logger.info("The client provides no roots: %s", exc)
        return result
    for root in getattr(lr, "roots", []) or []:
        uri = getattr(root, "uri", None)
        if uri is None:
            continue
        p = file_uri_to_path(str(uri))
        if p is not None and p.is_absolute():
            try:
                resolved = p.resolve()
            except (OSError, RuntimeError):
                resolved = p
            if resolved.is_absolute():
                result.append(resolved)
        else:
            logger.info("Root of the client that isn't a local folder, ignored: %s", uri)
    logger.info("Roots of the client: %s", ", ".join(str(p) for p in result) or "none")
    return result


async def allowed_roots(ctx, configured: list[Path]) -> list[Path]:
    roots = list(configured)
    roots.extend(await client_roots(ctx))
    # de-duplicate while preserving order
    seen: set[str] = set()
    unique: list[Path] = []
    for r in roots:
        key = _norm(r)
        if key in seen:
            continue
        seen.add(key)
        unique.append(r)
    return unique
