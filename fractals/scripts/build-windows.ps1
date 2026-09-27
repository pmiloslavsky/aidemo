# Configures and builds a CMake preset inside the MSVC x64 environment.
#   .\scripts\build-windows.ps1                       # windows-release
#   .\scripts\build-windows.ps1 -Preset windows-release-cpu-only
param([string]$Preset = 'windows-release')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common-windows.ps1"

$vs = Find-VisualStudio
if (-not $vs) { throw 'Visual Studio 2022 with C++ tools not found (run setup-windows.ps1)' }
$cmake = Find-BuildTool 'cmake' $vs
$ninja = Find-BuildTool 'ninja' $vs
if (-not $cmake -or -not $ninja) { throw 'cmake/ninja not found (run setup-windows.ps1)' }

$vcvars = "$vs\VC\Auxiliary\Build\vcvars64.bat"
$src = Resolve-Path "$PSScriptRoot\.."
$ninjaDir = Split-Path $ninja
$nvcc = Find-Nvcc
$cudaDir = if ($nvcc) { Split-Path $nvcc } else { '' }

# vcvars64.bat runs vswhere.exe from PATH, which isn't always on it.
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"

# One cmd session so the vcvars environment applies to both cmake steps.
# cmd expands %PATH% once for the whole line, so extend PATH before vcvars
# (which then prepends the MSVC tools to it).
$cmdline = "set `"PATH=$vsInstaller;$ninjaDir;$cudaDir;%PATH%`" && call `"$vcvars`" >nul " +
           "&& cd /d `"$src`" " +
           "&& `"$cmake`" --preset $Preset && `"$cmake`" --build --preset $Preset"
cmd /c $cmdline
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

Write-Host "Built: $src\build\$Preset\bin\fractals.exe"
