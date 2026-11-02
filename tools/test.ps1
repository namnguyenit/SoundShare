param([switch]$RunMutedAudio, [switch]$IncludeDependencyTests)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue
if ($ctestCommand) { $ctestExe = $ctestCommand.Source } else {
    $vsLocator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vsLocator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ctestExe = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
}
Push-Location -LiteralPath $projectRoot
try {
    $ctestArgs = @('--preset','release','--output-on-failure')
    if (-not $IncludeDependencyTests) { $ctestArgs += @('-R','^(core|control_protocol|desktop_model|receiver_lifecycle)$') }
    & $ctestExe @ctestArgs
    if ($LASTEXITCODE -ne 0) { throw 'Automated tests failed.' }
    if ($RunMutedAudio) {
        & python tests/windows_smoke.py out/build/windows-x64/Release/SoundShare.Worker.exe --run-muted-audio --seconds 20 --report test-results/windows-smoke.json
        if ($LASTEXITCODE -ne 0) { throw 'Muted Windows audio smoke test failed.' }
    }
} finally { Pop-Location }
