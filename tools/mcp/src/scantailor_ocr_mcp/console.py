"""Console handling for Windows Ctrl+Break delivery."""

from __future__ import annotations

import ctypes
import sys


def has_console() -> bool:
    if sys.platform != "win32":
        return True
    return ctypes.windll.kernel32.GetConsoleCP() != 0


def ensure_console() -> None:
    """Make sure this process has a console, hidden, so Ctrl+Break can reach children.

    AllocConsole creates a new console and updates the process standard handles through
    SetStdHandle, but Python's sys.stdin/stdout keep their file descriptors (0/1) which
    still point at the original pipes. The MCP stdio protocol therefore keeps working.
    """
    if sys.platform != "win32":
        return
    kernel32 = ctypes.windll.kernel32
    if kernel32.GetConsoleCP() != 0:
        return
    if not kernel32.AllocConsole():
        return
    hwnd = kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, 0)  # SW_HIDE
