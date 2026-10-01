"""Opt-in WASAPI hardware test. Uses GUI commands only; never opens a COM port.
Disable Windows Listen first. --physical starts with Teensy unplugged, then
prompts for connection, unplug/replug. --default-change also requests a user
output change. It never changes Windows device settings itself.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--hardware', required=True, action='store_true')
parser.add_argument('--physical', action='store_true')
parser.add_argument('--reconnect', action='store_true', help='start connected, then request physical replug')
parser.add_argument('--absent-exit', action='store_true', help='start unplugged and exit during discovery backoff')
parser.add_argument('--default-change', action='store_true')
parser.add_argument('--seconds', type=int, default=15, help='final flow observation interval')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
log = Path(os.environ['APPDATA']) / 'BroTracker/BroTracker Terminal/brotracker-terminal.log'
u = C.WinDLL('user32', use_last_error=True)
u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
u.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT, C.POINTER(C.c_size_t)]
u.SendMessageTimeoutW.restype = C.c_ssize_t
callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
u.EnumWindows.argtypes = [callback, W.LPARAM]
process = subprocess.Popen([str(repo/'deploy/windows/BroTrackerTerminal.exe')], cwd=os.environ['TEMP'],
    env=dict(os.environ, PATH=os.path.join(os.environ['SystemRoot'], 'System32')))
window = None
max_response = 0

def text():
    return log.read_text(errors='replace') if log.exists() else ''

def wait(test, description, timeout=30):
    global max_response
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        assert process.poll() is None, 'terminal exited unexpectedly'
        if window:
            result = C.c_size_t(); began = time.monotonic()
            assert u.SendMessageTimeoutW(window, 0, 0, 0, 2, 1000, C.byref(result)), 'UI unresponsive'
            max_response = max(max_response, time.monotonic()-began)
        if test():
            print('PASS:', description, flush=True); return
        time.sleep(.05)
    raise RuntimeError(description+'\n'+text()[-5000:])

def find():
    global window
    @callback
    def visit(hwnd, unused):
        global window
        pid = W.DWORD(); title = C.create_unicode_buffer(128)
        u.GetWindowThreadProcessId(hwnd, C.byref(pid)); u.GetWindowTextW(hwnd, title, 128)
        if pid.value == process.pid and title.value == 'BroTracker Terminal': window = hwnd
        return True
    u.EnumWindows(visit, 0)
    return bool(window)

def key(vk, scan):
    assert u.PostMessageW(window, 0x100, vk, (scan << 16)|1)
    assert u.PostMessageW(window, 0x101, vk, (scan << 16)|0xC0000001)

def routes(): return text().count('Windows audio: routing started;')
def starts(): return text().count('RX: BTTEST1 STARTED')
def flow():
    return any(int(a)>1000 and int(b)>1000 for a,b in re.findall(r'nonzero=(\d+) rendered=(\d+)', text()))
def counters():
    values = re.findall(r'flow running=1 captured=(\d+) nonzero=(\d+) rendered=(\d+)', text())
    return tuple(map(int, values[-1])) if values else (0, 0, 0)
def outputs(): return re.findall(r'default multimedia render: .*endpoint=(.*)', text())

try:
    wait(find, 'native terminal window')
    if args.absent_exit:
        wait(lambda: 'waiting: no active capture endpoint' in text(), 'absent device detected')
        assert u.PostMessageW(window,0x10,0,0)
        assert process.wait(timeout=5)==0
        assert 'worker stopped; all audio endpoints released' in text()
        assert 'shutdown summary running=0' in text()
        print('PASS: exit interrupts discovery backoff; audio worker joined', flush=True)
        raise SystemExit(0)
    if args.physical:
        wait(lambda: 'waiting: no active capture endpoint' in text(), 'startup without Teensy; no microphone fallback')
        print('READY: connect Teensy now; leave terminal open.', flush=True)
    wait(lambda: routes()>0 and 'handshake accepted' in text(), 'USB audio routing and CDC handshake', 300 if args.physical else 30)
    assert starts()==0, 'automatic START replay'
    key(0x20,0x39); wait(lambda: starts()==1, 'START acknowledged')
    wait(flow, 'non-silent capture and render submissions', 20)
    key(0x20,0x39); wait(lambda: starts()==2, 'restart acknowledged')
    key(0x0D,0x1C); wait(lambda: 'STOP acknowledged; ready' in text(), 'STOP and stay')
    if args.physical or args.reconnect:
        old_routes=routes(); old_handshakes=text().count('handshake accepted'); mark=len(text())
        print('READY: unplug Teensy, wait two seconds, then reconnect.', flush=True)
        wait(lambda: 'disconnected/error:' in text()[mark:], 'CDC physical disconnect', 300)
        wait(lambda: 'endpoints released' in text()[mark:] or 'releasing endpoints' in text()[mark:], 'audio endpoint release', 30)
        wait(lambda: routes()>old_routes and text().count('handshake accepted')>old_handshakes, 'physical audio and CDC reconnect', 300)
        assert starts()==2, 'automatic START after reconnect'
        print('PASS: no automatic START replay after reconnect', flush=True)
    if args.default_change:
        old_output=outputs()[-1]; old_routes=routes(); mark=len(text())
        print('READY: change default multimedia output to a DIFFERENT non-Teensy endpoint.', flush=True)
        wait(lambda: routes()>old_routes and outputs()[-1]!=old_output, 'default output reacquired', 300)
        assert starts()==2, 'automatic START after default change'
        print('PASS: no firmware START caused by output change', flush=True)
    before = counters()
    key(0x20,0x39); wait(lambda: starts()==3, 'START after recovery')
    wait(lambda: all(now-old > 1000 for now,old in zip(counters(), before)),
         'fresh non-silent capture/render flow after recovery', 20)
    began = time.monotonic()
    wait(lambda: time.monotonic()-began >= args.seconds, 'sustained flow observation', args.seconds+5)
    # The natural sequence may finish during observation. Restart explicitly
    # so this last close must exercise acknowledged STOP rather than idle exit.
    previous = starts(); key(0x20,0x39)
    wait(lambda: starts()==previous+1, 'active playback armed for close')
    assert u.PostMessageW(window,0x10,0,0)
    assert process.wait(timeout=15)==0
    final=text()
    assert 'STOP acknowledged; EXIT' in final
    assert 'worker stopped; all audio endpoints released' in final
    assert 'shutdown summary running=0' in final
    assert 'serial handle closed' in final
    print('PASS: acknowledged STOP, audio worker joined, handles closed; max UI probe %.1fms' % (max_response*1000), flush=True)
finally:
    if process.poll() is None:
        if window: u.PostMessageW(window,0x10,0,0)
        try: process.wait(timeout=15)
        except subprocess.TimeoutExpired: process.terminate(); process.wait()
    (repo/'build/windows-audio-hardware.log').write_text(text())
