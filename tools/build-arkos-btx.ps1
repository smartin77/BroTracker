$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ((Get-Location).Provider.Name -ne 'FileSystem' -or (Get-Location).ProviderPath -ne $repo) {
    throw "Run this script from the BroTracker repository root: $repo"
}

$wsl = Get-Command wsl.exe -CommandType Application -TotalCount 1 -ErrorAction SilentlyContinue
if (!$wsl) {
    throw 'WSL is required. See tools/arkos/README.md for the existing Eoan ARM64 setup.'
}

# Run in the default WSL distribution. Bash receives the script on stdin
# so PowerShell does not reinterpret shell variables, paths or quoting.
@'
set -euo pipefail
die() { printf '%s\n' "$*" >&2; exit 1; }
[[ -f tools/arkos/prepare-package.sh ]] || die 'WSL cannot access the BroTracker checkout at the requested directory.'
for tool in sudo realpath update-binfmts unshare mount chroot python3 readelf file sha256sum; do
    command -v "$tool" >/dev/null || die "Missing WSL prerequisite: $tool. See tools/arkos/README.md."
done
root_path=${BROTRACKER_EOAN_ROOT:-$HOME/brotracker-eoan-arm64}
root=$(realpath -e -- "$root_path") || die "Missing Eoan ARM64 build root: $root_path. See tools/arkos/README.md."
[[ "$root" != / && -f "$root/etc/os-release" ]] &&
    grep -q '^VERSION_CODENAME=eoan$' "$root/etc/os-release" ||
    die "Expected an initialized Ubuntu Eoan ARM64 root at $root."
sudo -n true || die 'The Eoan build requires noninteractive sudo in WSL for binfmt and private chroot mounts.'
sudo -n update-binfmts --enable qemu-aarch64 || die 'Cannot enable the existing qemu-aarch64 binfmt entry.'
sudo -n chroot "$root" /bin/true || die "Cannot execute ARM64 binaries in $root. Check QEMU and binfmt registration."

BROTRACKER_ARKOS_DELIVERY_ONLY=1 bash tools/arkos/prepare-package.sh
built="$root/build/brotracker/BroTrackerTerminal"
deployed=deploy/arkos/BroTrackerTerminal
cmp -- "$built" "$deployed" || die 'Built and deployed ArkOS BTX executables differ.'
sha256sum -- "$built" "$deployed"
(cd deploy/arkos && sha256sum -c SHA256SUMS)
'@ | & $wsl.Source --cd $repo --exec bash -c 'tr -d "\\r" | bash'
if ($LASTEXITCODE -ne 0) {
    throw "ArkOS BTX build or verification failed (exit $LASTEXITCODE). See the output above and tools/arkos/README.md."
}
