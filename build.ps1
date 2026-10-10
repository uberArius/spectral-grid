param([switch]$Package, [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$cmakeTool = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeTool) { $cmakeExe = $cmakeTool.Source } else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path $vswhere)) { throw 'Install Visual Studio Build Tools with C++, Windows SDK and CMake. See README.md.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExe = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (!(Test-Path $cmakeExe)) { throw 'CMake not found. Add C++ CMake tools for Windows in Visual Studio Installer.' }
}
& $cmakeExe -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmakeExe --build build --config Release --parallel
if ($LASTEXITCODE) { throw 'Build failed.' }
if (!$SkipTests) {
    & (Join-Path (Split-Path $cmakeExe) 'ctest.exe') --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed.' }
}
& $cmakeExe --install build --config Release --prefix dist/SpectralGrid
if ($LASTEXITCODE) { throw 'Installation to dist failed.' }
if ($Package) {
    & (Join-Path (Split-Path $cmakeExe) 'cpack.exe') --config build/CPackConfig.cmake -C Release -B dist
    if ($LASTEXITCODE) { throw 'Packaging failed.' }
    & (Join-Path $PSScriptRoot 'package-source.ps1')
}
Write-Host 'Ready: dist\SpectralGrid\SpectralGrid.exe'
