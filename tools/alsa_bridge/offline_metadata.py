"""Bounded read-only discovery. Descriptive unknowns never become compatibility gates."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess


def read_fact(path):
    try:
        with Path(path).open('rb') as f:
            data=f.read(65537)
        if len(data)>65536: return dict(status='unknown',reason='Discovery file exceeds 64 KiB',source=str(path))
        return dict(status='known',source=str(path),value=data.decode('utf-8',errors='replace').replace('\x00',' | ').strip())
    except OSError as e: return dict(status='unknown',source=str(path),reason=str(e))


def command_fact(command):
    # Spool stdout/stderr to a temporary file: no unbounded pipe buffering.
    import tempfile
    if not shutil.which(command[0]): return dict(status='unknown',reason=command[0]+' unavailable')
    try:
        with tempfile.TemporaryFile() as f:
            result=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=3)
            f.seek(0); data=f.read(65537)
        if result.returncode or len(data)>65536:
            return dict(status='unknown',reason='Command failed or output exceeds 64 KiB',command=command,returncode=result.returncode)
        return dict(status='known',command=command,value=data.decode(errors='replace'))
    except (OSError,subprocess.TimeoutExpired) as e: return dict(status='unknown',reason=str(e),command=command)


def collect(repo,port=None):
    result=dict(schema=1,kernel=dict(status='known',value=list(os.uname())),
        os_release=read_fact('/etc/os-release'),console_model=read_fact('/sys/firmware/devicetree/base/model'),
        console_compatible=read_fact('/sys/firmware/devicetree/base/compatible'),
        usb_topology=command_fact(['lsusb','-t']),usb_devices=command_fact(['lsusb']),
        alsa_cards=read_fact('/proc/asound/cards'),alsa_devices=read_fact('/proc/asound/pcm'),
        source_commit=command_fact(['git','--no-optional-locks','-C',str(repo),'rev-parse','HEAD']),
        source_state=command_fact(['git','--no-optional-locks','-C',str(repo),'status','--porcelain','--untracked-files=no']),
        verified_artifacts='artifacts.json contains verified binary, asset and source fingerprints',
        negotiated_audio='alsa-preflight.log and on/bridge-*/run.json contain actual negotiated ALSA parameters',
        configuration='config-preflight.txt records actual path; config-original.ini preserves existing bytes when present',
        workload='automatic-workload.json and condition readbacks contain device-verified pattern, tempo, gains and available sample metadata/checksums',
        phases_and_ownership='events.jsonl and condition result.json contain monotonic boundaries and owned PIDs',
        audible_click_status='NOT YET CONFIRMED')
    for key in ['physical_wiring','headphone_connection','analog_volume']:
        result[key]=dict(status='unknown',reason='Not observable through existing read-only software interfaces; not a run prerequisite')
    if port:
        device=(Path('/sys/class/tty')/Path(port).name/'device').resolve()
        result['serial_port']=port
        result['usb_ancestry']=[]
        for parent in [device]+list(device.parents):
            if (parent/'idVendor').exists():
                result['usb_ancestry'].append(dict(path=str(parent),attributes={name:read_fact(parent/name)
                    for name in ['idVendor','idProduct','manufacturer','product','serial','speed','busnum','devnum']}))
        if not result['usb_ancestry']: result['usb_identity']=dict(status='unknown',reason='USB sysfs ancestry not available')
    return result


def supplementary(path,destination):
    result=dict(status='unknown',reason='No optional user description available',verified=False)
    if path is None: return result
    result['source']=str(path)
    try:
        with Path(path).open('rb') as f: data=f.read(16385)
        if len(data)>16384:
            result['reason']='Optional context exceeds 16 KiB; original untouched'; return result
    except OSError as e:
        result['reason']=str(e); return result
    target=Path(destination)/'supplementary-user-manifest.original'
    # Failure to preserve evidence is a real recording failure, not unknown metadata.
    with target.open('xb') as f: f.write(data)
    result.update(status='available',reason='Unverified user description only; FILL values are allowed',
                  preserved_file=target.name,sha256=hashlib.sha256(data).hexdigest())
    return result
