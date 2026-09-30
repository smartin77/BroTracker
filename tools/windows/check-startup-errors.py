"""Exercise visible startup failure reporting; never open a serial port."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import shutil
import subprocess
import time

repo = Path(__file__).resolve().parents[2]
package = repo / 'deploy/windows'
work = repo / 'build/windows-startup-errors'
work.mkdir(exist_ok=True)
for source in [package / 'BroTrackerTerminal.exe', *package.glob('*.dll')]:
    shutil.copy2(source, work / source.name)
user = C.WinDLL('user32', use_last_error=True)
callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [callback, W.LPARAM]
user.EnumChildWindows.argtypes = [W.HWND, callback, W.LPARAM]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
log = Path(os.environ['APPDATA']) / 'BroTracker/BroTracker Terminal/brotracker-terminal.log'
original = log.read_bytes() if log.exists() else None
try:
    for mode in ['missing-font', 'bad-video-driver']:
        env = dict(os.environ)
        if mode == 'bad-video-driver': env['SDL_VIDEODRIVER'] = 'brotracker-invalid-test-driver'
        binary = work / 'BroTrackerTerminal.exe' if mode == 'missing-font' else package / 'BroTrackerTerminal.exe'
        process = subprocess.Popen([str(binary)], cwd=os.environ['TEMP'], env=env)
        dialog = [None]
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not dialog[0]:
                @callback
                def visit(hwnd, unused):
                    pid = W.DWORD(); user.GetWindowThreadProcessId(hwnd, C.byref(pid))
                    title = C.create_unicode_buffer(512); user.GetWindowTextW(hwnd, title, 512)
                    if pid.value == process.pid and title.value == 'BroTracker Terminal - Error': dialog[0] = hwnd
                    return True
                user.EnumWindows(visit, 0); time.sleep(.05)
            assert dialog[0], 'missing visible error dialog'
            texts = []
            @callback
            def child(hwnd, unused):
                value = C.create_unicode_buffer(2048); user.GetWindowTextW(hwnd, value, 2048)
                texts.append(value.value); return True
            user.EnumChildWindows(dialog[0], child, 0)
            message = '\n'.join(texts)
            assert 'Log:' in message and str(log) in message
            assert 'Font load failed' in message if mode == 'missing-font' else 'SDL initialization failed' in message
            assert 'Terminal error:' in log.read_text()
            assert 'Windows CDC:' not in log.read_text()
            user.PostMessageW(dialog[0], 0x10, 0, 0)  # close the native message box
            assert process.wait(timeout=10) == 1
            print('PASS: visible dialog, log path, file diagnostic, exit 1:', mode)
        finally:
            if process.poll() is None:
                if dialog[0]: user.PostMessageW(dialog[0], 0x10, 0, 0)
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.terminate(); process.wait()
finally:
    if original is not None: log.write_bytes(original)
