param([int]$SoakSeconds = 0)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$exe = Join-Path $PSScriptRoot 'build\Release\SpectralGrid.exe'
if (!(Test-Path $exe)) { throw 'Run build.ps1 first.' }
$app = Start-Process -FilePath $exe -ArgumentList '--smoke-test' -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
$app.WaitForExit()
if ($app.ExitCode -ne 0) { throw "Window test exited with code $($app.ExitCode). Check test-output\error.txt." }
$report = Get-Content 'test-output\integration.json' -Raw | ConvertFrom-Json
if (!$report.lightGpuValidation.passed) { throw 'GPU brightness and overlap test failed.' }
if (!$report.gravityGpuValidation.passed) { throw 'GPU/CPU gravity agreement test failed.' }
if ($report.stages.'0'.smoothingEnabled -ne $false) { throw 'Smoothing must be disabled by default.' }
if ($report.stages.'10'.columnsPerSecond -ne 240 -or $report.stages.'10'.fpsLimit -ne 240 -or $report.stages.'10'.smoothingEnabled -ne $false -or $report.stages.'11'.smoothingEnabled -ne $true) {
    throw 'Smoothing toggle and 240 range test failed.'
}
if ($report.stages.'10'.trebleBoostDb -ne 18) { throw 'Treble boost test failed.' }
foreach ($stage in @('1','2','8')) {
    if (!$report.stages.$stage.compositionPassed) { throw "Windows composition failed at stage $stage." }
}
Write-Host "GPU gravity, window and composition checks passed. GPU: $($report.gpu). Report: test-output\integration.json"
if ($SoakSeconds -gt 0) {
    $app = Start-Process -FilePath $exe -ArgumentList @('--benchmark', $SoakSeconds) -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
    $app.WaitForExit()
    if ($app.ExitCode -ne 0) { throw "Benchmark exited with code $($app.ExitCode)." }
    $report = Get-Content 'test-output\benchmark.json' -Raw | ConvertFrom-Json
    Write-Host "Benchmark: $($report.seconds) s, $($report.averageFps) FPS. Report: test-output\benchmark.json"
}
