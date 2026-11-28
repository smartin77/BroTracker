"""Host-only artifact-placement verification. No firmware build or device access.

Requires fresh case directories. Hash historical output trees before any configure,
then exercise default Debug/Release builds and an explicit cached override.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/btt-stopped-save/artifact-isolation-correction/verified'
SUITES={
    'BroTrackerBttResourceTests':'btt-resource-admission',
    'BroTrackerBttBackingTests':'btt-backing-prototype',
    'BroTrackerBttFoundationTests':'btt-foundation',
    'BroTrackerBttPatternLayoutTests':'btt-pattern-layout',
}
HISTORICAL=[ROOT/'build'/name for name in SUITES.values()]
COMMANDS=[]
COMMAND_RECORD='commands.json'


def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
    return value.hexdigest()


def snapshot():
    result={}
    for folder in HISTORICAL:
        assert folder.is_dir(),folder
        records={}
        for path in sorted([folder,*folder.rglob('*')]):
            stat=path.lstat();name=path.relative_to(folder).as_posix()
            if path.is_symlink():record=dict(kind='symlink',target=str(path.readlink()))
            elif path.is_file():record=dict(kind='file',bytes=stat.st_size,sha256=digest(path))
            else:record=dict(kind='directory')
            record['modified_ns']=stat.st_mtime_ns
            records[name]=record
        encoded=json.dumps(records,sort_keys=True,separators=(',',':')).encode()
        result[folder.relative_to(ROOT).as_posix()]=dict(sha256=hashlib.sha256(encoded).hexdigest(),entries=records)
    return result


def runtime_hashes():
    return {p.relative_to(ROOT).as_posix():digest(p)
        for folder in ('src','firmware','tools/btt') for p in (ROOT/folder).rglob('*')
        if p.is_file() and p.suffix in ('.h','.cpp','.ino')}


def run(args,log,cwd=ROOT,expected=0):
    command=[str(a) for a in args]
    process=subprocess.run(command,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (OUT/log).write_bytes(process.stdout)
    COMMANDS.append(dict(command=command,cwd=str(cwd),exit=process.returncode,log=log))
    (OUT/COMMAND_RECORD).write_text(json.dumps(COMMANDS,indent=2))
    assert process.returncode==expected,(log,process.returncode,process.stdout[-3000:])
    return process.stdout.decode(errors='replace')


def case(name,mode,override=None):
    build=OUT/name
    assert not build.exists(),f'Require a fresh out-of-source build: {build}'
    args=['cmake','-S',ROOT,'-B',build,'-G','MinGW Makefiles',f'-DCMAKE_BUILD_TYPE={mode}',
        '-DBROTRACKER_BUILD_ARKOS_UI=OFF','-DBROTRACKER_BUILD_WINDOWS_TERMINAL=OFF',
        '-DBROTRACKER_BUILD_FONT_PREVIEW=OFF']
    if override is not None:args+=['-DBROTRACKER_BTT_TEST_ARTIFACT_ROOT='+override.as_posix()]
    run(args,name+'-configure.log')
    if override is not None:
        # Ordinary reconfiguration must preserve a user's cached explicit path.
        run(['cmake','-S',ROOT,'-B',build],name+'-reconfigure.log')
    expected=(override or build/'artifacts').resolve()
    cache=(build/'CMakeCache.txt').read_text()
    actual=next(line.split('=',1)[1] for line in cache.splitlines()
        if line.startswith('BROTRACKER_BTT_TEST_ARTIFACT_ROOT:PATH='))
    assert Path(actual).resolve()==expected,(actual,expected)
    # CTest omits resolved command arrays until the target executables exist.
    run(['cmake','--build',build,'--target',*SUITES,'-j','8'],name+'-build.log')
    registration=json.loads(run(['ctest','--test-dir',build,'--show-only=json-v1'],name+'-registrations.json'))
    selected={test['name']:test for test in registration['tests'] if test['name'] in SUITES}
    assert set(selected)==set(SUITES)
    for suite,test in selected.items():
        assert len(test['command'])==2,test['command']
        assert Path(test['command'][1]).resolve()==expected/SUITES[suite],test['command']
    pattern='^('+'|'.join(SUITES)+')$'
    run(['ctest','--test-dir',build,'-R',pattern,'--output-on-failure'],name+'-tests.log')
    primary={'btt-resource-admission':'allocations.csv','btt-backing-prototype':'allocations.csv',
        'btt-foundation':'measurements.csv','btt-pattern-layout':'measurements.csv'}
    counts={}
    for folder,report in primary.items():
        assert (expected/folder/report).stat().st_size>0
        counts[folder]=sum(p.is_file() for p in (expected/folder).rglob('*'))
    # Test payloads/reports found anywhere in the binary tree must be within
    # the selected root. In the override case there should be none outside it.
    for p in build.rglob('*'):
        if p.is_file() and (p.suffix in ('.btt','.csv') or p.name in ('host-memory.txt','memory.txt')):
            assert p.resolve().is_relative_to(expected),p
    if override is not None:assert not (build/'artifacts').exists()
    # Direct generator invocation cannot silently fall back to shared paths.
    empty=build/'no-argument-invocation';empty.mkdir()
    for suite,test in selected.items():
        message=run([test['command'][0]],name+'-'+suite+'-no-argument.log',cwd=empty,expected=2)
        assert 'artifact-directory' in message
        assert not list(empty.iterdir())
    print(f'{name}: 4/4 suites passed; artifact root {expected}',flush=True)
    return dict(build=str(build),artifact_root=str(expected),override=override is not None,
        passed=4,artifact_file_counts=counts,no_argument_rejected_without_files=True)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    assert not (OUT/'historical-before.json').exists(),'Preserve existing verification; use its results'
    before=snapshot();runtime_before=runtime_hashes()
    (OUT/'historical-before.json').write_text(json.dumps(before,indent=2))
    (OUT/'runtime-before.json').write_text(json.dumps(runtime_before,indent=2))
    results=[];failure=None
    try:
        results.append(case('default-debug','Debug'))
        results.append(case('default-release','Release'))
        results.append(case('explicit-override','Release',OUT/'override output'))
    except Exception as error:
        failure=repr(error)
        raise
    finally:
        after=snapshot();runtime_after=runtime_hashes()
        (OUT/'historical-after.json').write_text(json.dumps(after,indent=2))
        (OUT/'runtime-after.json').write_text(json.dumps(runtime_after,indent=2))
        unchanged=before==after
        summary=dict(cases=results,historical_unchanged=unchanged,runtime_unchanged=runtime_before==runtime_after,
            historical_tree_sha256={name:entry['sha256'] for name,entry in before.items()},failure=failure)
        (OUT/'results.json').write_text(json.dumps(summary,indent=2))
        print(json.dumps(summary,indent=2),flush=True)
        assert unchanged,'Historical file/directory content, membership or modification time changed'
        assert runtime_before==runtime_after,'Runtime source changed'


def adapter_check():
    # The audit also found a sibling-shared CSV path in the SD source suite.
    # Reuse the freshly verified configurations, retaining their cached roots.
    global COMMAND_RECORD
    COMMAND_RECORD='adapter-commands.json'
    HISTORICAL.append(ROOT/'build/btt-source-adapter')
    before=snapshot();runtime_before=runtime_hashes()
    (OUT/'adapter-historical-before.json').write_text(json.dumps(before,indent=2))
    cases=json.loads((OUT/'results.json').read_text())
    assert cases['failure'] is None and len(cases['cases'])==3
    results=[]
    try:
        for case_result in cases['cases']:
            build=Path(case_result['build']);expected=Path(case_result['artifact_root'])/'btt-source-adapter/adapter-allocations.csv'
            name=build.name
            run(['cmake','-S',ROOT,'-B',build],name+'-adapter-reconfigure.log')
            run(['cmake','--build',build,'--target','BroTrackerBttSdSourceTests','-j','8'],name+'-adapter-build.log')
            registration=json.loads(run(['ctest','--test-dir',build,'--show-only=json-v1'],name+'-adapter-registrations.json'))
            test=next(t for t in registration['tests'] if t['name']=='BroTrackerBttSdSourceTests')
            assert len(test['command'])==2 and Path(test['command'][1]).resolve()==expected.resolve()
            run(['ctest','--test-dir',build,'-R','^BroTrackerBttSdSourceTests$','--output-on-failure'],name+'-adapter-tests.log')
            assert expected.stat().st_size>0 and Path(str(expected)+'.layout.txt').stat().st_size>0
            assert not (build.parent/'adapter-allocations.csv').exists()
            empty=build/'no-argument-invocation'
            message=run([test['command'][0]],name+'-adapter-no-argument.log',cwd=empty,expected=2)
            assert 'artifact-file' in message and not list(empty.iterdir())
            results.append(dict(build=str(build),output=str(expected),passed=True))
            print(name+': SD adapter output isolated; explicit cached root preserved',flush=True)
    finally:
        after=snapshot()
        (OUT/'adapter-historical-after.json').write_text(json.dumps(after,indent=2))
        summary=dict(cases=results,historical_unchanged=before==after,runtime_unchanged=runtime_before==runtime_hashes())
        (OUT/'adapter-results.json').write_text(json.dumps(summary,indent=2))
        assert summary['historical_unchanged'] and summary['runtime_unchanged']


if __name__=='__main__':
    if len(sys.argv)==2 and sys.argv[1]=='--adapter-check':adapter_check()
    else:main()
