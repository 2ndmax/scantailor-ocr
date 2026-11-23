"""Resolving and checking allowed folders."""

from __future__ import annotations

import os
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse


def resolve_path(path: str | Path) -> Path:
    return Path(path).expanduser().resolve()


def _norm(p: Path) -> str:
    s = str(p)
    if sys.platform == "win32":
        s = s.casefold()
    return s


def is_allowed(path: str | Path, allowed: list[Path]) -> bool:
    """True if the resolved path equals an allowed folder or lies inside it."""
    try:
        resolved = resolve_path(path)
    except (OSError, RuntimeError):
        return False
    target = _norm(resolved)
    for root in allowed:
        try:
            root_resolved = root.resolve()
        except (OSError, RuntimeError):
            continue
        base = _norm(root_resolved)
        if target == base:
            return True
        if target.startswith(base + os.sep) or target.startswith(base + "/"):
            return True
    return False


def file_uri_to_path(uri: str) -> Path | None:
    parsed = urlparse(uri)
    if parsed.scheme != "file":
        return None
    path = unquote(parsed.path)
    if sys.platform == "win32" and path.startswith("/"):
        path = path[1:]
    return Path(path)


async def client_roots(ctx) -> list[Path]:
    """Best-effort roots from the client session."""
    result: list[Path] = []
    try:
        lr = await ctx.session.list_roots()
    except Exception:
        return result
    for root in getattr(lr, "roots", []) or []:
        uri = getattr(root, "uri", None)
        if uri is None:
            continue
        p = file_uri_to_path(str(uri))
        if p is not None:
            try:
                result.append(p.resolve())
            except (OSError, RuntimeError):
                result.append(p)
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
