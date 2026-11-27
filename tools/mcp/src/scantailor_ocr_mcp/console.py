"""Console handling for Windows Ctrl+Break delivery."""

from __future__ import annotations

import ctypes
import logging
import sys

logger = logging.getLogger(__name__)


def ensure_console() -> bool:
    """Ensure the server process has a console on Windows.

    Windows only delivers ``CTRL_BREAK_EVENT`` across processes attached to the
    same console. When launched by an IDE or desktop client via stdio without
    a console, allocating a hidden console allows ``GenerateConsoleCtrlEvent``
    to reach child CLI processes started in the same console group.
    """
    if sys.platform != "win32":
        return True

    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetConsoleCP.argtypes = []
    kernel32.GetConsoleCP.restype = wintypes.UINT
    kernel32.AllocConsole.argtypes = []
    kernel32.AllocConsole.restype = wintypes.BOOL
    kernel32.GetConsoleWindow.argtypes = []
    kernel32.GetConsoleWindow.restype = wintypes.HWND

    if kernel32.GetConsoleCP() != 0:
        return True
    if not kernel32.AllocConsole():
        err = ctypes.get_last_error()
        logger.warning("AllocConsole failed with Win32 error %d", err)
        return False

    hwnd = kernel32.GetConsoleWindow()
    if hwnd:
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        user32.ShowWindow.restype = wintypes.BOOL
        user32.ShowWindow(hwnd, 0)  # SW_HIDE
    return True
