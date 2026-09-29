#!/usr/bin/env bash
# Run inside the Eoan ARM64 build root (see README.md).
set -euo pipefail
export LC_ALL=C
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=${BROTRACKER_ARKOS_BUILD_DIR:-/build/brotracker}
package_dir=${BROTRACKER_ARKOS_PACKAGE_DIR:-/package/arkos}
jobs=${BROTRACKER_BUILD_JOBS:-2}

for tool in cmake make gcc g++ pkg-config readelf file dpkg-query; do
    command -v "$tool" >/dev/null || { echo "Missing build-root tool: $tool" >&2; exit 1; }
done
# Reject newer libraries rather than silently producing an incompatible package.
[[ $(dpkg --print-architecture) == arm64 ]]
[[ $(getconf GNU_LIBC_VERSION) == "glibc 2.30" ]]
[[ $(dpkg-query -W -f='${Version}' libstdc++6) == 9.2.1-9ubuntu2 ]]
[[ $(dpkg-query -W -f='${Version}' libsdl2-dev) == 2.0.10+dfsg1-1ubuntu1 ]]
[[ $(gcc -dumpfullversion) == 9.2.1 ]]
unset CMAKE_PREFIX_PATH PKG_CONFIG_SYSROOT_DIR LD_LIBRARY_PATH LD_PRELOAD
export PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig
[[ $(pkg-config --modversion sdl2) == 2.0.10 ]]

cmake -S "$repo_root" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON \
    -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config \
    -DBROTRACKER_BUILD_ARKOS_UI=ON
# Verify every existing CMake target with Eoan's compiler and CMake.
cmake --build "$build_dir" --parallel "$jobs"
(cd "$build_dir" && ctest --output-on-failure)

binary="$build_dir/BroTrackerArkOSUI"
readelf -h "$binary" | grep -q 'Machine:.*AArch64'
readelf -l "$binary" | grep -q 'Requesting program interpreter: /lib/ld-linux-aarch64.so.1]'
if readelf -d "$binary" | grep -Eq '\((RPATH|RUNPATH)\)'; then
    echo 'Unexpected runtime search path in the executable' >&2
    exit 1
fi
needed=$(readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
grep -qx 'libSDL2-2.0.so.0' <<< "$needed"
while IFS= read -r library; do
    case "$library" in
        libSDL2-2.0.so.0|libstdc++.so.6|libgcc_s.so.1|libc.so.6|ld-linux-aarch64.so.1|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1) ;;
        *) echo "Unexpected dependency: $library" >&2; exit 1 ;;
    esac
done <<< "$needed"

versions=$(readelf --version-info "$binary" | grep -oE '(GLIBC|GLIBCXX|CXXABI)_[0-9.]+' | sort -Vu)
while IFS= read -r version; do
    case "$version" in
        GLIBC_*) ceiling=2.30 ;;
        GLIBCXX_*) ceiling=3.4.28 ;;
        CXXABI_*) ceiling=1.3.12 ;;
    esac
    dpkg --compare-versions "${version#*_}" le "$ceiling" || {
        echo "Unsupported symbol version: $version" >&2; exit 1;
    }
done <<< "$versions"

# Resolve all dependencies and relocations against the matching Eoan libraries.
ldd -r "$binary" > "$build_dir/arkos-dependencies.txt" 2>&1
if grep -Eq 'not found|undefined symbol' "$build_dir/arkos-dependencies.txt"; then
    cat "$build_dir/arkos-dependencies.txt" >&2
    exit 1
fi
file "$binary"
readelf -l "$binary" | grep 'Requesting program interpreter'
printf 'NEEDED:\n%s\nRequired versions:\n%s\n' "$needed" "$versions"
cat "$build_dir/arkos-dependencies.txt"

# Verify the separately built, checked bridge already versioned in the package.
# Keep ALSA out of the UI target and preserve this exact bridge executable.
binary="$repo_root/deploy/arkos/BroTrackerAlsaBridge"
readelf -h "$binary" | grep -q 'Machine:.*AArch64'
readelf -l "$binary" | grep -q 'Requesting program interpreter: /lib/ld-linux-aarch64.so.1]'
if readelf -d "$binary" | grep -Eq '\((RPATH|RUNPATH)\)'; then
    echo 'Unexpected runtime search path in the executable' >&2
    exit 1
fi
needed=$(readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
grep -qx 'libasound.so.2' <<< "$needed"
while IFS= read -r library; do
    case "$library" in
        libasound.so.2|libstdc++.so.6|libgcc_s.so.1|libc.so.6|ld-linux-aarch64.so.1|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1) ;;
        *) echo "Unexpected dependency: $library" >&2; exit 1 ;;
    esac
done <<< "$needed"

versions=$(readelf --version-info "$binary" | grep -oE '(GLIBC|GLIBCXX|CXXABI)_[0-9.]+' | sort -Vu)
while IFS= read -r version; do
    case "$version" in
        GLIBC_*) ceiling=2.30 ;;
        GLIBCXX_*) ceiling=3.4.28 ;;
        CXXABI_*) ceiling=1.3.12 ;;
    esac
    dpkg --compare-versions "${version#*_}" le "$ceiling" || {
        echo "Unsupported symbol version: $version" >&2; exit 1;
    }
done <<< "$versions"

# Resolve all dependencies and relocations against the matching Eoan libraries.
ldd -r "$binary" > "$build_dir/alsa-bridge-dependencies.txt" 2>&1
if grep -Eq 'not found|undefined symbol' "$build_dir/alsa-bridge-dependencies.txt"; then
    cat "$build_dir/alsa-bridge-dependencies.txt" >&2
    exit 1
fi
file "$binary"
readelf -l "$binary" | grep 'Requesting program interpreter'
printf 'NEEDED:\n%s\nRequired versions:\n%s\n' "$needed" "$versions"
cat "$build_dir/alsa-bridge-dependencies.txt"

# Stage application files only, after all checks passed. Never copy SDL2.
mkdir -p "$package_dir/assets/fonts"
cp "$build_dir/BroTrackerArkOSUI" "$package_dir/BroTrackerArkOSUI"
cp "$binary" "$package_dir/BroTrackerAlsaBridge"
cp "$repo_root/assets/fonts/brotracker.btf" "$repo_root/assets/fonts/brotracker.bfm" "$package_dir/assets/fonts/"
cp "$repo_root/assets/dummy_my_tune.json" "$package_dir/assets/"
cp "$repo_root/tools/arkos/launch.sh" "$package_dir/launch.sh"
chmod +x "$package_dir/BroTrackerArkOSUI" "$package_dir/BroTrackerAlsaBridge" "$package_dir/launch.sh"
echo "Eoan ABI checks passed; staged at $package_dir. Console display/input still need a hardware test."
