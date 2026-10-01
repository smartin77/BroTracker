#!/usr/bin/env bash
# Isolated package/upgrade checks; never write /roms or run packaged binaries.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/repo/tools/arkos/port" "$work/repo/deploy" "$work/ports/brotracker"
cp -a "$repo/deploy/arkos" "$work/repo/deploy/"
cp "$repo/tools/arkos/port/BroTracker Terminal.sh" "$work/repo/tools/arkos/port/"
# Change only the destination in a disposable copy; production paths stay fixed.
sed "s|^ports=/roms/ports$|ports=$work/ports|" "$repo/tools/arkos/install-port.sh" > "$work/repo/tools/arkos/install-port.sh"
installer="$work/repo/tools/arkos/install-port.sh"
app="$work/ports/brotracker"
new_launcher="$work/ports/BroTracker Terminal.sh"
legacy_launcher="$work/ports/BroTracker.sh"
printf "preserved backup\n" > "$work/ports/BroTracker.sh.bak.saved"
printf 'old launcher\n' > "$legacy_launcher"
printf 'old executable\n' > "$app/BroTrackerArkOSUI"
printf 'user data\n' > "$app/user-song.json"
printf 'unrelated executable\n' > "$app/custom-tool"
cp "$app/user-song.json" "$work/user-original"
cp "$app/custom-tool" "$work/tool-original"
bash "$installer"
[[ ! -e "$app/BroTrackerArkOSUI" ]]
backups=("$work/ports/"BroTracker.sh.bak.*)
[[ ${#backups[@]} == 2 ]]
[[ ! -e "$legacy_launcher" ]]
[[ -f "$new_launcher" ]]
old_backup=$(find "$work/ports" -name 'BroTracker.sh.bak.*' ! -name '*.saved')
printf 'old launcher\n' | cmp - "$old_backup"
for file in BroTrackerTerminal BroTrackerAlsaBridge launch.sh assets/dummy_my_tune.json assets/fonts/brotracker.btf assets/fonts/brotracker.bfm; do
    cmp "$repo/deploy/arkos/$file" "$app/$file"
done
for file in BroTrackerTerminal BroTrackerAlsaBridge launch.sh; do
    [[ $(stat -c %a "$app/$file") == 755 ]]
done
cmp "$repo/tools/arkos/port/BroTracker Terminal.sh" "$new_launcher"
[[ $(stat -c %a "$new_launcher") == 755 ]]
cmp "$app/user-song.json" "$work/user-original"
cmp "$app/custom-tool" "$work/tool-original"
# Repeated update backs up only the new entry and preserves legacy backups.
bash "$installer"
backups=("$work/ports/"BroTracker.sh.bak.*)
[[ ${#backups[@]} == 2 ]]
new_backups=("$work/ports/"BroTracker\ Terminal.sh.bak.*)
[[ ${#new_backups[@]} == 1 ]]
[[ ! -e "$legacy_launcher" ]]
printf "preserved backup\n" | cmp - "$work/ports/BroTracker.sh.bak.saved"
cmp "$app/user-song.json" "$work/user-original"
cmp "$app/custom-tool" "$work/tool-original"
# A failed new-launcher install must retain and back up the legacy entry.
printf 'legacy retained on failure\n' > "$legacy_launcher"
mkdir "$work/bin"
cat > "$work/bin/install" <<'STUB'
#!/bin/bash
if [[ "${@: -1}" == *"/BroTracker Terminal.sh" ]]; then exit 42; fi
exec /usr/bin/install "$@"
STUB
chmod +x "$work/bin/install"
if PATH="$work/bin:$PATH" bash "$installer" > "$work/failure" 2>&1; then
    echo 'Expected launcher installation failure' >&2; exit 1
fi
printf 'legacy retained on failure\n' | cmp - "$legacy_launcher"
failed_backup=$(find "$work/ports" -name 'BroTracker.sh.bak.*' ! -name '*.saved' ! -path "$old_backup")
printf 'legacy retained on failure\n' | cmp - "$failed_backup"
# When both entries exist, successful upgrade retains one active launcher.
bash "$installer"
[[ ! -e "$legacy_launcher" && -f "$new_launcher" ]]
cmp "$repo/tools/arkos/port/BroTracker Terminal.sh" "$new_launcher"
printf "preserved backup\n" | cmp - "$work/ports/BroTracker.sh.bak.saved"
backup_count=$(find "$work/ports" -name '*.bak.*' | wc -l)
# An unsafe obsolete destination must reject the upgrade before any writes.
ln -s "$work/user-original" "$app/BroTrackerArkOSUI"
if bash "$installer" > "$work/error" 2>&1; then echo 'Accepted obsolete symlink' >&2; exit 1; fi
grep -q 'Refusing a symlink destination' "$work/error"
[[ $(find "$work/ports" -name '*.bak.*' | wc -l) == "$backup_count" ]]
cmp "$app/user-song.json" "$work/user-original"
# Direct and Ports launchers must resolve the current packaged executable.
grep -q 'exec ./BroTrackerTerminal' "$app/launch.sh"
grep -q 'BROTRACKER_APPEND_LOG=1 ./BroTrackerTerminal' "$new_launcher"
! grep -q 'BroTrackerArkOSUI' "$app/launch.sh" "$new_launcher"
echo 'Installer/package upgrade checks passed (isolated temporary destination)'
