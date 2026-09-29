#!/bin/bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
package="$repo_root/deploy/arkos"
launcher="$repo_root/tools/arkos/port/BroTracker.sh"
ports=/roms/ports
app="$ports/brotracker"
port_launcher="$ports/BroTracker.sh"
files=(
    BroTrackerArkOSUI
    BroTrackerAlsaBridge
    launch.sh
    assets/dummy_my_tune.json
    assets/fonts/brotracker.btf
    assets/fonts/brotracker.bfm
)

fail() { echo "Error: $*" >&2; exit 1; }

# Check all sources and destinations before writing anything.
[[ -d "$ports" ]] || fail "Ports directory is missing: $ports"
[[ -f "$launcher" ]] || fail "Launcher is missing: $launcher"
for file in "${files[@]}"; do
    [[ -f "$package/$file" ]] || fail "Package file is missing: $file"
done
for dir in "$app" "$app/assets" "$app/assets/fonts"; do
    [[ ! -L "$dir" ]] || fail "Refusing a symlink directory: $dir"
    [[ ! -e "$dir" || -d "$dir" ]] || fail "Not a directory: $dir"
done
targets=("$port_launcher")
for file in "${files[@]}"; do
    targets+=("$app/$file")
done
for target in "${targets[@]}"; do
    [[ ! -L "$target" ]] || fail "Refusing a symlink destination: $target"
    [[ ! -e "$target" || -f "$target" ]] || fail "Not a regular file: $target"
done

# Use a unique backup name; never overwrite an earlier launcher backup.
if [[ -f "$port_launcher" ]]; then
    backup=$(mktemp "$ports/BroTracker.sh.bak.XXXXXX")
    cp -p -- "$port_launcher" "$backup"
    echo "Previous launcher saved as: $backup"
fi

mkdir -p -- "$app/assets/fonts"
for file in "${files[@]}"; do
    case "$file" in
        BroTrackerArkOSUI|BroTrackerAlsaBridge|launch.sh) mode=0755 ;;
        *) mode=0644 ;;
    esac
    install -m "$mode" -- "$package/$file" "$app/$file"
done
install -m 0755 -- "$launcher" "$port_launcher"

echo "Installed BroTracker in $app"
echo "Ports launcher: $port_launcher"
