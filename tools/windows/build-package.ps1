param(
    [string]$ToolchainBin = (Split-Path (Get-Command g++.exe -ErrorAction Stop).Source),
    [string]$BuildDirectory = "build"
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$build = [IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory))
$package = Join-Path $repo "deploy/windows"
$env:PATH = "$ToolchainBin;$env:PATH"
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
& "$ToolchainBin/cmake.exe" -S $repo -B $build -G "MinGW Makefiles" `
    "-DCMAKE_CXX_COMPILER=$ToolchainBin/g++.exe" "-DCMAKE_C_COMPILER=$ToolchainBin/gcc.exe" `
    "-DCMAKE_MAKE_PROGRAM=$ToolchainBin/mingw32-make.exe" `
    "-DSDL2_DIR=$ToolchainBin/../lib/cmake/SDL2" -DBROTRACKER_BUILD_WINDOWS_TERMINAL=ON
Check-Exit
# A compiler-path change can make CMake regenerate the cache and discard -D
# options on that first pass. Reapply target options before building/packaging.
& "$ToolchainBin/cmake.exe" -S $repo -B $build `
    "-DSDL2_DIR=$ToolchainBin/../lib/cmake/SDL2" -DBROTRACKER_BUILD_WINDOWS_TERMINAL=ON
Check-Exit
& "$ToolchainBin/cmake.exe" --build $build --parallel 4
Check-Exit
& "$ToolchainBin/ctest.exe" --test-dir $build --output-on-failure
Check-Exit
New-Item -ItemType Directory -Force $package | Out-Null
Copy-Item -LiteralPath "$build/BroTrackerTerminal.exe" -Destination $package
# Resolve the full transitive runtime closure from this exact toolchain.
# Windows system/UCRT DLLs remain supplied by Windows, never copied.
$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue("$package/BroTrackerTerminal.exe")
$seen = @{}
while ($queue.Count) {
    $binary = $queue.Dequeue()
    $imports = & "$ToolchainBin/objdump.exe" -p $binary
    Check-Exit
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $dll = $Matches[1]
        if ($seen.ContainsKey($dll)) { continue }
        $seen[$dll] = $true
        if ($dll -match '^(api-ms-|ext-ms-)' -or (Test-Path "$env:SystemRoot/System32/$dll")) { continue }
        $source = Join-Path $ToolchainBin $dll
        if (!(Test-Path -LiteralPath $source)) { throw "Missing runtime dependency: $dll" }
        Copy-Item -LiteralPath $source -Destination $package
        $queue.Enqueue((Join-Path $package $dll))
    }
}
foreach ($asset in @("assets/dummy_my_tune.json", "assets/fonts/brotracker.btf", "assets/fonts/brotracker.bfm")) {
    $destination = Join-Path $package $asset
    New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo $asset) -Destination $destination
}
Copy-Item -LiteralPath "$repo/LICENSE" -Destination $package
New-Item -ItemType Directory -Force "$package/licenses" | Out-Null
foreach ($license in @("SDL2", "gcc-libs", "libwinpthread")) {
    Copy-Item -LiteralPath "$ToolchainBin/../share/licenses/$license" -Destination "$package/licenses" -Recurse -Force
}
Write-Host "Runnable package: $package/BroTrackerTerminal.exe"
Get-FileHash -Algorithm SHA256 "$package/BroTrackerTerminal.exe"
