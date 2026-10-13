param(
    [ValidateSet('doctor','setup','generate','build','all','run')][string]$Action = 'doctor',
    [string]$Disc1,
    [string]$Disc2,
    [string]$BuildRoot,
    [string]$LLVMRoot,
    [string]$Python = 'python',
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This workflow supports Windows x64.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools and Windows SDK first; see README.txt.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools were not found.' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
if ($LLVMRoot) { $env:PATH = "$(Join-Path $LLVMRoot 'bin');$env:PATH" }
$arguments = @((Join-Path $PSScriptRoot 'pipeline.py'), $Action, '--jobs', "$Jobs")
if ($Disc1) { $arguments += @('--disc1', $Disc1) }
if ($Disc2) { $arguments += @('--disc2', $Disc2) }
if ($BuildRoot) { $arguments += @('--build-root', $BuildRoot) }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw "FM4 workflow failed (exit $LASTEXITCODE). See the build-root logs." }
