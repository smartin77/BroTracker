param(
    [ValidateSet('ON', 'OFF')]
    [string]$EmptyTitle = 'ON'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ((Get-Location).Provider.Name -ne 'FileSystem' -or (Get-Location).ProviderPath -ne $repo) {
    throw "Run this script from the BroTracker repository root: $repo"
}

$cmake = Get-Command cmake.exe -CommandType Application -TotalCount 1 -ErrorAction SilentlyContinue
if (!$cmake) {
    throw 'CMake is missing from PATH. Open a shell with the Windows BTX build tools available.'
}

$EmptyTitle = $EmptyTitle.ToUpperInvariant()
$build = Join-Path $repo 'build/windows-current'
$deployed = Join-Path $repo 'deploy/windows/BroTrackerTerminal.exe'
& $cmake.Source -S $repo -B $build -DCMAKE_BUILD_TYPE=Release `
    -DBROTRACKER_BUILD_WINDOWS_TERMINAL=ON "-DBROTRACKER_WINDOWS_EMPTY_TITLE=$EmptyTitle"
if ($LASTEXITCODE -ne 0) {
    throw "Windows BTX configure failed (exit $LASTEXITCODE)."
}

& $cmake.Source --build $build --config Release --target BroTrackerTerminal --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Windows BTX build failed (exit $LASTEXITCODE)."
}

# CMake places multi-config outputs in a configuration subdirectory.
$outputDir = $build
if (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_CONFIGURATION_TYPES:[^=]+=.+') {
    $outputDir = Join-Path $build 'Release'
}
$built = Join-Path $outputDir 'BroTrackerTerminal.exe'
if (!(Test-Path -LiteralPath $built -PathType Leaf)) {
    throw "Expected build output is missing: $built"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $deployed) | Out-Null
try {
    [IO.File]::Copy($built, $deployed, $true)
} catch {
    throw "Cannot replace '$deployed'. If BroTrackerTerminal is running, close it and rerun the script. $($_.Exception.Message)"
}

$builtHash = (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash
$deployedHash = (Get-FileHash -LiteralPath $deployed -Algorithm SHA256).Hash
if ($builtHash -ne $deployedHash) {
    throw "Built and deployed Windows BTX hashes differ: $builtHash / $deployedHash"
}

Write-Host "Deployed: $deployed"
Write-Host "Timestamp: $((Get-Item -LiteralPath $deployed).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
Write-Host "SHA-256 (build and deployment): $deployedHash"
Write-Host "EmptyTitle: $EmptyTitle"
