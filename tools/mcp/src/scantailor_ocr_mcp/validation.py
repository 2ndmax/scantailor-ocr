"""Shared validation of the CLI's JSON output."""

from __future__ import annotations

import json


def validate_cli_json(text: str) -> dict:
    """Parse text as the CLI's JSON output and require schemaVersion 1."""
    try:
        data = json.loads(text)
    except (json.JSONDecodeError, ValueError) as exc:
        raise ValueError("CLI did not produce valid JSON") from exc
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError("CLI produced an unexpected schema")
    return data
