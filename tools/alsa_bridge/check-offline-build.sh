#!/usr/bin/env bash
# Run only in the existing Eoan ARM64 environment, after focused builds/tests.
set -euo pipefail
build=$(realpath -e -- "$1")
report="$build/offline-abi-checks.txt"
: > "$report"
for name in BroTrackerAlsaBridge BroTrackerTerminal; do
    binary="$build/$name"
    readelf -h "$binary" | grep -q 'Machine:.*AArch64'
    readelf -l "$binary" | grep -q 'interpreter: /lib/ld-linux-aarch64.so.1]'
    if readelf -d "$binary" | grep -Eq '\((RPATH|RUNPATH)\)'; then exit 1; fi
    versions=$(readelf --version-info "$binary" | grep -oE '(GLIBC|GLIBCXX|CXXABI)_[0-9.]+' | sort -Vu)
    while IFS= read -r version; do
        case "$version" in GLIBC_*) ceiling=2.30;; GLIBCXX_*) ceiling=3.4.28;; CXXABI_*) ceiling=1.3.12;; esac
        dpkg --compare-versions "${version#*_}" le "$ceiling"
    done <<< "$versions"
    ldd -r "$binary" > "$build/$name-offline-dependencies.txt" 2>&1
    if grep -Eq 'not found|undefined symbol' "$build/$name-offline-dependencies.txt"; then exit 1; fi
    needed=$(readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
    if [[ $name == BroTrackerTerminal ]]; then grep -qx 'libSDL2-2.0.so.0' <<< "$needed";
    else grep -qx 'libasound.so.2' <<< "$needed"; fi
    while IFS= read -r library; do
        case "$library" in
            libSDL2-2.0.so.0|libasound.so.2|libstdc++.so.6|libgcc_s.so.1|libc.so.6|ld-linux-aarch64.so.1|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1) ;;
            *) echo "Unexpected dependency: $library" >&2; exit 1;;
        esac
    done <<< "$needed"
    readelf -d "$binary" >> "$report"
    printf '%s\n' "$versions" >> "$report"
    (cd "$build" && sha256sum "$name") >> "$report"
done
"$build/BroTrackerAlsaBridge" --diagnostic-info | grep -q 'bridge-clicks-offline-v2'
"$build/BroTrackerTerminal" --offline-diagnostic-info | grep -q 'btx-offline-clicks-v2'
printf 'OFFLINE ABI CHECKS PASSED\n' >> "$report"
cat "$report"
