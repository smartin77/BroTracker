#!/usr/bin/env bash
# Validate the actual Git-delivered package, never repair a test fixture.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
package="$repo/deploy/arkos"
files=(BroTrackerTerminal BroTrackerAlsaBridge launch.sh config/config.ini.example
       assets/dummy_my_tune.json assets/fonts/brotracker.btf assets/fonts/brotracker.bfm)
for file in "${files[@]}" SHA256SUMS bridge-clicks-artifacts.json; do
    [[ -f "$package/$file" && ! -L "$package/$file" ]] || { echo "Missing/unsafe delivery file: $file" >&2; exit 1; }
    # Untracked new files are allowed before review, ignored delivery files are not.
    if git -C "$repo" check-ignore --no-index -q -- "deploy/arkos/$file"; then
        echo "Git ignores required delivery file: $file" >&2; exit 1
    fi
    clean=$(git -C "$repo" hash-object --path="deploy/arkos/$file" -- "$package/$file")
    raw=$(git -C "$repo" hash-object --no-filters -- "$package/$file")
    [[ "$clean" == "$raw" ]] || { echo "Git would change delivery bytes: $file" >&2; exit 1; }
done
(cd "$package" && sha256sum -c SHA256SUMS)
# Require an exact manifest, not just a passing subset of the package.
diff -u <(printf '%s\n' "${files[@]}" | sort) <(sed 's/^[0-9a-f]\{64\}  //' "$package/SHA256SUMS" | sort)
for name in BroTrackerTerminal BroTrackerAlsaBridge; do
    readelf -h "$package/$name" | grep -q 'Machine:.*AArch64'
done
for file in assets/dummy_my_tune.json assets/fonts/brotracker.btf assets/fonts/brotracker.bfm config/config.ini.example; do
    cmp "$repo/$file" "$package/$file"
done
cmp "$repo/tools/arkos/launch.sh" "$package/launch.sh"
# Test the shipped executable's embedded protocol, in addition to ARM64 functional tests.
strings "$package/BroTrackerTerminal" | grep -F 'BTMIDI1 SET 1 %u' >/dev/null
python3 - "$package" "$repo" <<'PY'
import hashlib, json, sys
from pathlib import Path
p=Path(sys.argv[1]); repo=Path(sys.argv[2]); manifest=json.loads((p/'bridge-clicks-artifacts.json').read_text())
for name,digest in manifest['sha256'].items():
    assert hashlib.sha256((p/name).read_bytes()).hexdigest()==digest, name
for name,digest in manifest['source_sha256'].items():
    assert hashlib.sha256((repo/name).read_bytes().replace(b'\r\n',b'\n')).hexdigest()==digest, name
for name,digest in manifest['assets_sha256'].items():
    data=(p/name).read_bytes()
    if Path(name).suffix in ('.json','.btf'): data=data.replace(b'\r\n',b'\n')
    assert hashlib.sha256(data).hexdigest()==digest, name
print('Packaged ARM64 identities, required assets, Git visibility and MIDI protocol capability verified')
PY
