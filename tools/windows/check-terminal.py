"""Opt-in Windows window + connected-Teensy test; sends BTTEST1 via UI only.
No COM enumeration/opening here: production discovery is exercised by the app.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import time
import struct
import re

parser = argparse.ArgumentParser()
parser.add_argument('--hardware', action='store_true', required=True,
                    help='authorize START/restart/STOP on the attached Teensy')
parser.add_argument('--reconnect', action='store_true', help='wait for a physical unplug/replug after handshake')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
exe = repo / 'deploy/windows/BroTrackerTerminal.exe'
log = Path(os.environ['APPDATA']) / 'BroTracker/BroTracker Terminal/brotracker-terminal.log'
user = C.WinDLL('user32', use_last_error=True)
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
user.GetWindowLongW.argtypes = [W.HWND, C.c_int]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [callback_type, W.LPARAM]
user.SendMessageTimeoutW.restype = C.c_ssize_t
user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
# IMAGE_OPTIONAL_HEADER.Subsystem offset is 68 for both PE32 and PE32+.
image = exe.read_bytes()
pe = struct.unpack_from('<I', image, 0x3c)[0]
assert struct.unpack_from('<H', image, pe + 24 + 68)[0] == 2
print('PASS: PE Windows GUI subsystem (no console suppression flags)', flush=True)
process = None
window = None

def text():
    return log.read_text(errors='replace') if log.exists() else ''

def wait_for(predicate, label, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            print('PASS:', label, flush=True)
            return
        time.sleep(.05)
    raise RuntimeError(label + '\n' + text()[-6000:])

def launch():
    global process, window
    # No MSYS toolchain on PATH and an unrelated working directory: package
    # must supply runtime DLLs and locate its own assets.
    env = dict(os.environ, PATH=os.path.join(os.environ['SystemRoot'], 'System32'))
    process = subprocess.Popen([str(exe)], cwd=os.environ['TEMP'], env=env)
    window = None
    def find():
        global window
        @callback_type
        def visit(hwnd, unused):
            global window
            pid = W.DWORD()
            user.GetWindowThreadProcessId(hwnd, C.byref(pid))
            title = C.create_unicode_buffer(128)
            user.GetWindowTextW(hwnd, title, 128)
            if pid.value == process.pid and title.value == 'BroTracker Terminal':
                window = hwnd
            return True
        user.EnumWindows(visit, 0)
        return window is not None
    wait_for(find, 'native BroTracker Terminal window')
    rect = W.RECT()
    assert user.GetClientRect(window, C.byref(rect))
    assert (rect.right, rect.bottom) == (640, 480)
    assert not user.GetWindowLongW(window, -16) & 0x40000  # WS_THICKFRAME
    print('PASS: 640x480 client area, non-resizable', flush=True)
    wait_for(lambda: 'handshake accepted' in text(), 'automatic identity discovery and BTTEST1 handshake')

def key(vk, scan):
    assert user.PostMessageW(window, 0x100, vk, (scan << 16) | 1)
    assert user.PostMessageW(window, 0x101, vk, (scan << 16) | 0xC0000001)

def started(count):
    wait_for(lambda: text().count('RX: BTTEST1 STARTED') >= count, 'START/restart acknowledged ' + str(count))

def finish():
    assert process.wait(timeout=15) == 0
    assert 'serial handle closed' in text()
    print('PASS: clean exit, serial handle closed', flush=True)

try:
    launch()
    if args.reconnect:
        print('READY: unplug Teensy, wait two seconds, reconnect it.', flush=True)
        response_times = []
        def responsive(predicate):
            result = C.c_size_t()
            start = time.monotonic()
            C.set_last_error(0)
            ok = user.SendMessageTimeoutW(window, 0, 0, 0, 2, 1000, C.byref(result))
            assert ok, 'UI probe failed: Win32=%s process=%s' % (C.get_last_error(), process.poll())
            response_times.append(time.monotonic() - start)
            return predicate()
        wait_for(lambda: responsive(lambda: 'disconnected/error:' in text()), 'physical USB disconnect detected', 300)
        wait_for(lambda: responsive(lambda: text().count('handshake accepted') >= 2), 'physical USB reconnect handshake', 300)
        after_disconnect = text().split('disconnected/error:', 1)[1]
        assert 'RX: BTTEST1 STATE IDLE' in after_disconnect, 'handshake did not report idle startup'
        assert 'START requested' not in text() and 'queued for TX: START' not in text(), 'automatic START replay'
        print('PASS: idle startup handshake; no automatic START replay; max UI probe %.1f ms' % (max(response_times)*1000), flush=True)
        (repo / 'build/windows-physical-reconnect.log').write_text(text())
        for line in after_disconnect.splitlines():
            if 'tick=' in line: print(line, flush=True)
    key(0x20, 0x39); started(1)
    user.PostMessageW(window, 0x100, 0x20, (0x39 << 16) | 1)
    started(2)  # leave Space held for the repeat check
    assert 'RESTART queued via START' in text()
    # Repeat bit and unrelated volume key must not send additional commands.
    user.PostMessageW(window, 0x100, 0x20, (0x39 << 16) | 0x40000001)
    user.PostMessageW(window, 0x101, 0x20, (0x39 << 16) | 0xC0000001)
    key(0xAF, 0)  # VK_VOLUME_UP
    time.sleep(.2)
    assert text().count('RX: BTTEST1 STARTED') == 2
    print('PASS: auto-repeat and volume key ignored', flush=True)
    key(0x0D, 0x1C)
    wait_for(lambda: 'STOP acknowledged; ready' in text(), 'Enter STOP acknowledgement')
    assert process.poll() is None
    key(0x0D, 0x1C)
    time.sleep(.2)
    assert process.poll() is None
    assert text().count('RX: BTTEST1 STOPPED') == 1
    print('PASS: Enter while stopped stays without duplicate STOP', flush=True)
    key(0x20, 0x39); started(3)
    assert user.PostMessageW(window, 0x10, 0, 0)  # WM_CLOSE -> SDL_QUIT
    finish()
    assert 'STOP acknowledged; EXIT' in text()
    (repo / 'build/windows-hardware-first.log').write_text(text())
    # Reopen the same device without resetting the engine. This verifies release
    # and late attach; physical USB replug is a separate manual check.
    launch()
    assert user.PostMessageW(window, 0x10, 0, 0)
    finish()
    assert 'EXIT: no STOP necessary' in text()
    (repo / 'build/windows-hardware-reopen.log').write_text(text())
    launch()
    key(0x20, 0x39); started(1)
    user.PostMessageW(window, 0x100, 0x11, (0x1D << 16) | 1)  # Ctrl down
    key(0x58, 0x2D)
    user.PostMessageW(window, 0x101, 0x11, (0x1D << 16) | 0xC0000001)
    finish()
    assert 'EXIT requested by Ctrl+X' in text()
    assert 'STOP acknowledged; EXIT' in text()
    (repo / 'build/windows-hardware-ctrl-x.log').write_text(text())
    launch()
    key(0x20, 0x39)
    key(0x20, 0x39)
    user.PostMessageW(window, 0x10, 0, 0)
    finish()
    assert text().count('RX: BTTEST1 STARTED') == 1
    assert text().count('RX: BTTEST1 STOPPED') == 1
    assert 'STOP acknowledged; EXIT' in text()
    print('PASS: rapid duplicate START plus close preserves START -> STOP', flush=True)
    (repo / 'build/windows-hardware-queued-close.log').write_text(text())
finally:
    if process is not None and process.poll() is None:
        if window: user.PostMessageW(window, 0x10, 0, 0)
        try: process.wait(timeout=15)
        except subprocess.TimeoutExpired: process.terminate(); process.wait()
