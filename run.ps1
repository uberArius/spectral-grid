$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'dist\SpectralGrid\SpectralGrid.exe'
if (!(Test-Path $exe)) { & (Join-Path $PSScriptRoot 'build.ps1') }
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
