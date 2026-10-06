#!/bin/bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
package="$repo_root/deploy/arkos"
launcher="$repo_root/tools/arkos/port/BroTracker Terminal.sh"
diagnostics="$repo_root/tools/arkos/port/BroTracker Diagnostics.sh"
ports=/roms/ports
app="$ports/brotracker"
port_launcher="$ports/BroTracker Terminal.sh"
diagnostic_launcher="$ports/BroTracker Diagnostics.sh"
legacy_launcher="$ports/BroTracker.sh"
obsolete="$app/BroTrackerArkOSUI"
files=(
    BroTrackerTerminal
    BroTrackerAlsaBridge
    launch.sh
    assets/dummy_my_tune.json
    assets/fonts/brotracker.btf
    assets/fonts/brotracker.bfm
)

fail() { echo "Error: $*" >&2; exit 1; }

# Advisory only: sudo's effective user is not necessarily the Ports runtime user.
runtime_user=${BROTRACKER_RUNTIME_USER:-${SUDO_USER:-${USER:-}}}
if command -v getent >/dev/null && getent group dialout >/dev/null; then
    if [[ -z "$runtime_user" || "$runtime_user" == root ]]; then
        echo 'Advisory: runtime user is unknown; check dialout membership for the user running Ports.' >&2
        echo 'Setup: sudo usermod -aG dialout <runtime-user>; restart existing sessions or reboot.' >&2
    elif groups=$(id -nG "$runtime_user" 2>/dev/null); then
        if [[ " $groups " != *" dialout "* ]]; then
            echo "Advisory: Ports runtime user $runtime_user lacks dialout membership." >&2
            printf 'Setup: sudo usermod -aG dialout %q; restart existing sessions or reboot.\n' "$runtime_user" >&2
        fi
    else
        echo "Advisory: cannot check groups for runtime user $runtime_user; check dialout membership manually." >&2
    fi
fi

# Check all sources and destinations before writing anything.
[[ -d "$ports" ]] || fail "Ports directory is missing: $ports"
[[ -f "$launcher" ]] || fail "Launcher is missing: $launcher"
[[ -f "$diagnostics" ]] || fail "Diagnostics launcher is missing: $diagnostics"
for file in "${files[@]}"; do
    [[ -f "$package/$file" ]] || fail "Package file is missing: $file"
done
for dir in "$app" "$app/assets" "$app/assets/fonts"; do
    [[ ! -L "$dir" ]] || fail "Refusing a symlink directory: $dir"
    [[ ! -e "$dir" || -d "$dir" ]] || fail "Not a directory: $dir"
done
targets=("$diagnostic_launcher" "$port_launcher" "$legacy_launcher" "$obsolete")
for file in "${files[@]}"; do
    targets+=("$app/$file")
done
for target in "${targets[@]}"; do
    [[ ! -L "$target" ]] || fail "Refusing a symlink destination: $target"
    [[ ! -e "$target" || -f "$target" ]] || fail "Not a regular file: $target"
done

# Use a unique backup name; never overwrite an earlier launcher backup.
for existing in "$diagnostic_launcher" "$port_launcher" "$legacy_launcher"; do
    if [[ -f "$existing" ]]; then
        backup=$(mktemp "$existing.bak.XXXXXX")
        cp -p -- "$existing" "$backup"
        echo "Previous launcher saved as: $backup"
    fi
done

mkdir -p -- "$app/assets/fonts"
for file in "${files[@]}"; do
    case "$file" in
        BroTrackerTerminal|BroTrackerAlsaBridge|launch.sh) mode=0755 ;;
        *) mode=0644 ;;
    esac
    install -m "$mode" -- "$package/$file" "$app/$file"
done
install -m 0755 -- "$launcher" "$port_launcher"
install -m 0755 -- "$diagnostics" "$diagnostic_launcher"
# Retire the legacy Ports entry only after the new launcher installs successfully.
rm -f -- "$legacy_launcher"
# Remove only the old executable, after the replacement package/launcher succeed.
rm -f -- "$obsolete"

echo "Installed BroTracker Terminal (BTX) in $app"
echo "Ports launcher: $port_launcher"
