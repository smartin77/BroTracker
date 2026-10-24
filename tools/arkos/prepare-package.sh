#!/usr/bin/env bash
# WSL/Linux build + verified repository delivery. No Git index or remote writes.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
root=$(realpath -e -- "${BROTRACKER_EOAN_ROOT:-$HOME/brotracker-eoan-arm64}")
cd "$repo"
# This runner deliberately keeps /workspace read-only while compiling/testing.
bash tools/arkos/run-in-eoan.sh
bash tools/arkos/run-in-eoan.sh bash /workspace/tools/alsa_bridge/check-offline-build.sh /build/brotracker
package="$root/package/arkos"
built="$root/build/brotracker"
files=(BroTrackerTerminal BroTrackerAlsaBridge launch.sh config/config.ini.example
       assets/dummy_my_tune.json assets/fonts/brotracker.btf assets/fonts/brotracker.bfm)
for file in "${files[@]}"; do
    [[ -f "$package/$file" && ! -L "$package/$file" ]] || { echo "Incomplete build package: $file" >&2; exit 1; }
done
cmp "$built/BroTrackerTerminal" "$package/BroTrackerTerminal"
cmp "$built/BroTrackerAlsaBridge" "$package/BroTrackerAlsaBridge"
for file in "${files[@]}"; do
    case "$file" in BroTrackerTerminal|BroTrackerAlsaBridge|launch.sh) mode=0755;; *) mode=0644;; esac
    install -D -m "$mode" -- "$package/$file" "$repo/deploy/arkos/$file"
done
# Refresh existing artifact identity metadata; no audio/bridge diagnostic run.
python3 tools/alsa_bridge/prepare_offline_artifacts.py --build-dir "$built" --package-dir deploy/arkos
(cd deploy/arkos && sha256sum "${files[@]}" > SHA256SUMS)
bash tools/arkos/check-package.sh
bash tools/arkos/check-install-port.sh
printf '\nVerified build staged in deploy/arkos, including config and checksums.\n'
printf 'Review all delivery files together; no Git files were staged or committed.\n'
