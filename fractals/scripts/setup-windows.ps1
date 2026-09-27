# Checks that the Windows build tools are present. Installs nothing unless
# -InstallCuda is passed (CI only). Never touches the NVIDIA driver.
param([switch]$InstallCuda)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common-windows.ps1"

$ok = $true

$vs = Find-VisualStudio
if ($vs) { Write-Host "OK   Visual Studio: $vs" }
else { Write-Host "MISSING Visual Studio 2022 with the 'Desktop development with C++' workload"; $ok = $false }

foreach ($tool in 'cmake', 'ninja') {
    $path = Find-BuildTool $tool $vs
    if ($path) { Write-Host "OK   ${tool}: $path" }
    else { Write-Host "MISSING $tool (install it, or use the copy bundled with Visual Studio)"; $ok = $false }
}

$nvcc = Find-Nvcc
if ($nvcc) {
    Write-Host "OK   nvcc: $nvcc"
} elseif ($InstallCuda) {
    # CI only: the network installer with just the compiler and runtime pieces.
    $ver = Get-CiCudaVersion
    Write-Host "Installing CUDA $ver (nvcc + cudart only)"
    & "$PSScriptRoot\install-cuda-ci.ps1" -Version $ver
} else {
    Write-Host "INFO nvcc not found: the build will be CPU-only (CUDA is never installed locally)"
}

if (-not $ok) { exit 1 }
