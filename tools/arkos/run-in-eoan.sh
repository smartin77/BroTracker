#!/usr/bin/env bash
# Run a command in the Eoan root with temporary, private mounts.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
root=$(realpath -e -- "${BROTRACKER_EOAN_ROOT:-$HOME/brotracker-eoan-arm64}")
[[ "$root" != / && -f "$root/etc/os-release" ]]
grep -q '^VERSION_CODENAME=eoan$' "$root/etc/os-release"
if (( $# == 0 )); then
    set -- /bin/bash /workspace/tools/arkos/build-package.sh
fi
sudo unshare --mount --propagation private bash -s -- "$root" "$repo_root" "$@" <<'ROOT'
set -euo pipefail
root=$1
repo=$2
shift 2
mkdir -p "$root/workspace" "$root/proc" "$root/dev"
mount --bind "$repo" "$root/workspace"
mount -o remount,bind,ro "$root/workspace"
mount -t proc proc "$root/proc"
mount --rbind /dev "$root/dev"
mount --make-rslave "$root/dev"
chroot "$root" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LC_ALL=C DEBIAN_FRONTEND=noninteractive "$@"
ROOT
