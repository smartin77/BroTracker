#!/usr/bin/env bash
# Isolated package/upgrade checks; never write /roms or run packaged binaries.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/repo/tools/arkos/port" "$work/repo/deploy" "$work/ports/brotracker"
cp -a "$repo/deploy/arkos" "$work/repo/deploy/"
cp "$repo/tools/arkos/port/BroTracker.sh" "$work/repo/tools/arkos/port/"
# Change only the destination in a disposable copy; production paths stay fixed.
sed "s|^ports=/roms/ports$|ports=$work/ports|" "$repo/tools/arkos/install-port.sh" > "$work/repo/tools/arkos/install-port.sh"
installer="$work/repo/tools/arkos/install-port.sh"
app="$work/ports/brotracker"
printf 'old launcher\n' > "$work/ports/BroTracker.sh"
printf 'old executable\n' > "$app/BroTrackerArkOSUI"
printf 'user data\n' > "$app/user-song.json"
printf 'unrelated executable\n' > "$app/custom-tool"
cp "$app/user-song.json" "$work/user-original"
cp "$app/custom-tool" "$work/tool-original"
bash "$installer"
[[ ! -e "$app/BroTrackerArkOSUI" ]]
backups=("$work/ports/"BroTracker.sh.bak.*)
[[ ${#backups[@]} == 1 ]]
printf 'old launcher\n' | cmp - "${backups[0]}"
for file in BroTrackerTerminal BroTrackerAlsaBridge launch.sh assets/dummy_my_tune.json assets/fonts/brotracker.btf assets/fonts/brotracker.bfm; do
    cmp "$repo/deploy/arkos/$file" "$app/$file"
done
for file in BroTrackerTerminal BroTrackerAlsaBridge launch.sh; do
    [[ $(stat -c %a "$app/$file") == 755 ]]
done
cmp "$repo/tools/arkos/port/BroTracker.sh" "$work/ports/BroTracker.sh"
[[ $(stat -c %a "$work/ports/BroTracker.sh") == 755 ]]
cmp "$app/user-song.json" "$work/user-original"
cmp "$app/custom-tool" "$work/tool-original"
# Repeated update creates another backup and preserves unrelated data.
bash "$installer"
backups=("$work/ports/"BroTracker.sh.bak.*)
[[ ${#backups[@]} == 2 ]]
cmp "$app/user-song.json" "$work/user-original"
cmp "$app/custom-tool" "$work/tool-original"
# An unsafe obsolete destination must reject the upgrade before any writes.
ln -s "$work/user-original" "$app/BroTrackerArkOSUI"
if bash "$installer" > "$work/error" 2>&1; then echo 'Accepted obsolete symlink' >&2; exit 1; fi
grep -q 'Refusing a symlink destination' "$work/error"
backups=("$work/ports/"BroTracker.sh.bak.*)
[[ ${#backups[@]} == 2 ]]
cmp "$app/user-song.json" "$work/user-original"
# Direct and Ports launchers must resolve the current packaged executable.
grep -q 'exec ./BroTrackerTerminal' "$app/launch.sh"
grep -q 'BROTRACKER_APPEND_LOG=1 ./BroTrackerTerminal' "$work/ports/BroTracker.sh"
! grep -q 'BroTrackerArkOSUI' "$app/launch.sh" "$work/ports/BroTracker.sh"
echo 'Installer/package upgrade checks passed (isolated temporary destination)'
