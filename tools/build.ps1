param([ValidateRange(1,32)][int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakeExe = $cmakeCommand.Source
} else {
    $vsLocator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsLocator)) { throw 'Install Visual Studio C++ Build Tools and CMake.' }
    $vsPath = & $vsLocator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExe = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmakeExe)) { throw 'CMake was not found in the C++ Build Tools installation.' }
}
Push-Location -LiteralPath $projectRoot
try {
    & $cmakeExe --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmakeExe --build --preset release --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    Write-Output (Join-Path $projectRoot 'out\build\windows-x64\Release\SoundShare.Desktop.exe')
    Write-Output (Join-Path $projectRoot 'out\build\windows-x64\Release\SoundShare.Worker.exe')
} finally { Pop-Location }
