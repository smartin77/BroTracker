"""Compile/link only. No upload or physical operations. Run from repo root.

prepare: isolated production map/stack config. Build it with pio before measure.
measure: actual adapter, target layout, stack reports and identical-baseline links
with explicit roots, with/without isolated incremental RAM2 reservation.
"""
import configparser
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/btt-save-backend'
HERE = Path(__file__).resolve().parent


def run(args, name):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / name).write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{name}: exit {result.returncode}\n{result.stdout[-3000:]}')
    return result.stdout


def prepare():
    OUT.mkdir(parents=True, exist_ok=True)
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / 'platformio.ini')
    config['platformio']['build_dir'] = str(OUT / 'mapped')
    config['env:teensy41_dev']['extra_scripts'] = (
        '${env:teensy41.extra_scripts}\npost:tools/teensy_diagnostics/btt_admission/map_flags.py')
    config['env:teensy41_dev']['build_flags'] += '\n-fstack-usage'
    with (OUT / 'platformio.ini').open('w') as stream:
        config.write(stream)


def measure():
    info = json.loads((ROOT / '.pio/build/teensy41_dev/idedata.json').read_text())
    compiler = Path(info['cxx_path'])
    nm = compiler.parent / 'arm-none-eabi-nm.exe'
    size = compiler.parent / 'arm-none-eabi-size.exe'
    objdump = compiler.parent / 'arm-none-eabi-objdump.exe'
    flags = info['cxx_flags'] + ['-D' + d for d in info['defines']]
    for path in info['includes']['build'] + [str(ROOT), str(ROOT / 'tools/btt')]:
        flags += ['-I', path]
    commands = []
    for name, source in [('fs_save', ROOT / 'tools/btt/fs_save.cpp'), ('probe', HERE / 'probe.cpp')]:
        command = [compiler, *flags, '-fstack-usage', '-c', source, '-o', OUT / (name + '.o')]
        commands.append([str(a) for a in command])
        run(command, name + '-compile.log')
        run([nm, '-u', '-C', OUT / (name + '.o')], name + '-undefined.txt')
        run([objdump, '-drC', OUT / (name + '.o')], name + '-disassembly.txt')
    symbols = run([nm, '-S', str(OUT / 'probe.o')], 'layout-symbols.txt')
    layouts = {parts[3]: int(parts[1], 16) for line in symbols.splitlines()
               if len(parts := line.split()) == 4 and parts[3].startswith('size_')}
    (OUT / 'layouts.json').write_text(json.dumps(layouts, indent=2))
    baseline = OUT / 'mapped/teensy41_dev'
    original = json.loads((baseline / 'link-command.json').read_text())
    for name, roots in [('baseline', []), ('rooted', ['btt_fs_save_probe']),
                        ('reserved', ['btt_fs_save_probe', 'btt_fs_save_reservation'])]:
        command = original[:]
        command[command.index('-o') + 1] = str(OUT / (name + '.elf'))
        command = [arg for arg in command if not arg.startswith('-Wl,-Map,')]
        command += ['-Wl,-Map,' + str(OUT / (name + '.map'))]
        if roots:
            # Put new objects before the library group to resolve all references.
            at = next(i for i, arg in enumerate(command) if '--start-group' in arg)
            command[at:at] = [str(OUT / 'fs_save.o'), str(OUT / 'probe.o')]
            command += ['-Wl,-u,' + root for root in roots]
        commands.append(command)
        run(command, name + '-link.log')
        run([size, '-A', OUT / (name + '.elf')], name + '-sections.txt')
        run([nm, '-n', '-S', '-C', OUT / (name + '.elf')], name + '-symbols.txt')
    (OUT / 'target-commands.json').write_text(json.dumps(commands, indent=2))
    sections = {}
    for name in ('baseline', 'rooted', 'reserved'):
        rows = (OUT / (name + '-sections.txt')).read_text().splitlines()
        sections[name] = {p[0]: int(p[1]) for row in rows
                          if len(p := row.split()) == 3 and p[0].startswith('.')}
    memory = {}
    for name, s in sections.items():
        symbols = (OUT / (name + '-symbols.txt')).read_text().splitlines()
        addresses = {p[-1]: int(p[0], 16) for row in symbols
                     if len(p := row.split()) >= 3 and p[-1] in ('_stext', '_etext')}
        itcm = addresses['_etext'] - addresses['_stext']
        banks = (itcm + 32767) // 32768
        ram1 = s['.data'] + s['.bss']
        memory[name] = dict(itcm=itcm, itcm_banks=banks, ram1_static=ram1,
                            ram1_stack_gap=524288 - banks * 32768 - ram1,
                            ram2_static=s['.bss.dma'], ram2_heap_interval=524288 - s['.bss.dma'])
    (OUT / 'memory-summary.json').write_text(json.dumps(memory, indent=2))
    assert 'FsSave::Run(' not in (OUT / 'baseline-symbols.txt').read_text()
    assert 'FsSave::Run(' in (OUT / 'rooted-symbols.txt').read_text()
    assert 'btt::Save(' in (OUT / 'rooted-symbols.txt').read_text()
    assert memory['rooted']['ram2_static'] == memory['baseline']['ram2_static']
    reservation = (layouts['size_FsSave'] + layouts['size_Document'] + layouts['size_Workspace'] + 31) // 32 * 32
    assert memory['reserved']['ram2_static'] - memory['rooted']['ram2_static'] == reservation
    assert 'operator new(' not in (OUT / 'fs_save-undefined.txt').read_text()
    (OUT / 'target-checks.txt').write_text(
        'PASS: baseline excludes adapter; rooted ELF retains actual Run/Save.\n'
        'PASS: rooted code adds no static RAM2; isolated reservation matches layouts.\n'
        'PASS: adapter object has no allocating operator new reference.\n')
    # Collect stack frames from fresh production build and adapter compilation.
    frames = []
    for file in list(baseline.rglob('*.su')) + list(OUT.glob('*.su')):
        for line in file.read_text().splitlines():
            if any(word in line for word in ('FsSave', 'foundation.cpp', 'FsBackend', 'FsBaseFile', 'FatFile', 'ExFatFile')):
                frames.append(str(file.relative_to(ROOT)) + '\t' + line)
    (OUT / 'stack-frames.tsv').write_text('\n'.join(frames) + '\n')
    run([compiler, '--version'], 'target-compiler.txt')
    print(json.dumps(layouts, indent=2))


if __name__ == '__main__':
    {'prepare': prepare, 'measure': measure}[sys.argv[1]]()
