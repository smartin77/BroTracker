"""Opt-in Windows BTX input/exit hardware test; sends messages only to its own child.
Preserves config and development files. Modifies authorized session/draft test data.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from check_hardware import Device


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hardware',action='store_true',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    root=Path(__file__).resolve().parents[2]
    executable=root/'deploy/windows/BroTrackerTerminal.exe'
    log=Path(os.environ['APPDATA'])/'BroTracker'/'BroTracker Terminal'/'brotracker-terminal.log'
    user32=c.WinDLL('user32',use_last_error=True)
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user32.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    user32.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    user32.EnumWindows.argtypes=[callback,w.LPARAM]
    child=None
    def launch(name):
        nonlocal child
        if log.exists():shutil.copy2(log,args.output/(name+'-previous.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        child=subprocess.Popen([str(executable)],cwd=executable.parent,startupinfo=startup)
        deadline=time.monotonic()+12
        handles=[]
        @callback
        def match(hwnd,_):
            pid=w.DWORD();user32.GetWindowThreadProcessId(hwnd,c.byref(pid))
            if pid.value==child.pid:handles.append(hwnd)
            return True
        while time.monotonic()<deadline:
            user32.EnumWindows(match,0)
            if handles and log.exists() and 'Working pattern synchronized' in log.read_text(errors='replace'):
                return handles[0]
            if child.poll() is not None:raise RuntimeError('BTX exited unexpectedly')
            time.sleep(.1)
        raise RuntimeError('BTX did not synchronize within deadline; inspect log')
    def key(hwnd,vk):
        scan=user32.MapVirtualKeyW(vk,0);bits=1|(scan<<16)|((1<<24) if vk==0x22 else 0)
        assert user32.PostMessageW(hwnd,0x100,vk,bits)
        assert user32.PostMessageW(hwnd,0x101,vk,(1<<30)|(1<<31)|bits)
    def wait_text(text,seconds=10):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if text in log.read_text(errors='replace'):return
            time.sleep(.1)
        raise RuntimeError('Missing BTX result: '+text)
    def close(hwnd,name):
        assert user32.PostMessageW(hwnd,0x10,0,0)
        child.wait(timeout=20)
        assert child.returncode==0,child.returncode
        shutil.copy2(log,args.output/(name+'.log'))
    try:
        hwnd=launch('controls')
        initial=log.read_text(errors='replace')
        assert 'BTSTORE1 LOAD 1' not in initial and 'BTTEST1 STOP' not in initial and 'BTTEST1 START' not in initial
        key(hwnd,0x74);wait_text('Tune Save: ok')  # F5
        key(hwnd,0x72);wait_text('baseline readback verified: patern4')  # F3
        time.sleep(1)
        key(hwnd,0x73);wait_text('BTSTORE1 RESULT 1 draft-load ok')  # F4
        time.sleep(1)
        key(hwnd,0x22);time.sleep(1.5)  # Page Down, reversed config increments note.
        key(hwnd,0x74);time.sleep(1)
        key(hwnd,0x22);time.sleep(1.5)
        key(hwnd,0x20);wait_text('BTTEST1 STARTED')
        time.sleep(.5)
        close(hwnd,'windows-controls-exit')
        text=(args.output/'windows-controls-exit.log').read_text(errors='replace')
        exit_lines=text[text.index('Transport input: window close'):]
        assert 'TX complete: BTTEST1 STOP' not in exit_lines
        device=Device(args.output/'windows-after-exit.log')
        try:
            assert 'BTTEST1 STATE PLAYING' in device.hello()
            published=device.snapshot();working=device.snapshot(True)
            assert published!=working
            (args.output/'windows-running.json').write_text(json.dumps(dict(published=published,working=working),indent=2))
        finally:device.close()
        hwnd=launch('running-reconnect');time.sleep(1);close(hwnd,'windows-running-reconnect-exit')
        text=(args.output/'windows-running-reconnect-exit.log').read_text(errors='replace')
        assert 'TX complete: BTTEST1 STOP' not in text and 'TX complete: BTTEST1 START' not in text and 'TX complete: BTSTORE1 LOAD' not in text
        device=Device(args.output/'windows-settle.log')
        try:
            assert 'BTTEST1 STATE PLAYING' in device.hello()
            assert device.snapshot()==published and device.snapshot(True)==working
            device.request('BTTEST1 PAUSE','BTTEST1 PAUSED',1)
            device.request('BTTEST1 STOP','BTTEST1 STOPPED',1)
            device.request('BTSTORE1 SAVE 1','BTSTORE1 RESULT 1 save ok 0',1)
            (args.output/'expected-reboot.json').write_text(json.dumps(dict(published=published,working=working),indent=2))
        finally:device.close()
        print('PASS: actual Windows F3/F4/F5, unpublished preview, window-close exit without STOP, running BTX reconnect without LOAD/STOP/START')
        print('Prepared persisted distinct baseline/working state stopped for physical power-cycle')
    finally:
        if child and child.poll() is None:
            # Graceful close only our child; no unrelated windows/processes.
            handles=[]
            @callback
            def owned(hwnd,_):
                pid=w.DWORD();user32.GetWindowThreadProcessId(hwnd,c.byref(pid))
                if pid.value==child.pid:user32.PostMessageW(hwnd,0x10,0,0)
                return True
            user32.EnumWindows(owned,0)
            child.wait(timeout=20)


if __name__=='__main__':main()
