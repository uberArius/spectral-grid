$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$destination = Join-Path $PSScriptRoot 'dist\SpectralGrid-1.0.0-Source.zip'
New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create)
$archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $directories = @('src', 'shaders', 'resources', 'tests', 'docs', '.github')
    $files = @('CMakeLists.txt', 'CMakePresets.json', '.gitignore', '.clang-format', 'build.ps1', 'run.ps1', 'test-windows.ps1', 'package-source.ps1', 'README.md', 'ARCHITECTURE.md', 'TEST_REPORT.md', 'LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md', 'SECURITY.md', 'CHANGELOG.md')
    foreach ($directory in $directories) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -File -Recurse) {
            $relative = $file.FullName.Substring($PSScriptRoot.Length + 1).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, "SpectralGrid/$relative") | Out-Null
        }
    }
    foreach ($file in $files) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $PSScriptRoot $file), "SpectralGrid/$file") | Out-Null
    }
} finally { $archive.Dispose(); $stream.Dispose() }
Write-Host "Source archive: $destination"
