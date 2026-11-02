param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    & (Join-Path $PSScriptRoot 'test.ps1')
}
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExe = $cmakeCommand.Source } else {
    $vsLocator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vsLocator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExe = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$packageRoot = Join-Path $projectRoot 'out\desktop-package'
& $cmakeExe --install (Join-Path $projectRoot 'out\build\windows-x64') --config Release --prefix $packageRoot
if ($LASTEXITCODE -ne 0) { throw 'Packaging installation failed.' }
foreach ($executable in 'SoundShare.Desktop.exe','SoundShare.Worker.exe','SoundShare.Ctl.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot "bin\$executable"))) { throw "Missing $executable" }
}
$archivePath = Join-Path $projectRoot 'out\SoundShare-windows-x64.zip'
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $archivePath -Force
Get-FileHash -LiteralPath $archivePath -Algorithm SHA256 | Format-List
Write-Output $archivePath
