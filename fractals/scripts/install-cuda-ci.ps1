# CI only: installs the CUDA compiler and runtime pieces with NVIDIA's network
# installer (no driver, no Visual Studio integration). Called by
# setup-windows.ps1 -InstallCuda. Never run this on a dev machine.
param([Parameter(Mandatory)][string]$Version)   # full version, e.g. 12.9.1

$ErrorActionPreference = 'Stop'
$mm = ($Version -split '\.')[0..1] -join '.'   # 12.9

$url = "https://developer.download.nvidia.com/compute/cuda/$Version/network_installers/cuda_${Version}_windows_network.exe"
$exe = Join-Path $env:RUNNER_TEMP "cuda_${Version}_network.exe"
if (-not $env:RUNNER_TEMP) { $exe = Join-Path $env:TEMP "cuda_${Version}_network.exe" }

Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $exe -UseBasicParsing

$packages = 'nvcc', 'nvvm', 'crt', 'cudart', 'curand_dev' | ForEach-Object { "${_}_$mm" }
Write-Host "Installing: $($packages -join ' ')"
$p = Start-Process -FilePath $exe -ArgumentList (@('-s') + $packages) -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "CUDA installer failed ($($p.ExitCode))" }

$cudaPath = "${env:ProgramFiles}\NVIDIA GPU Computing Toolkit\CUDA\v$mm"
if (-not (Test-Path "$cudaPath\bin\nvcc.exe")) { throw "nvcc not found under $cudaPath" }
& "$cudaPath\bin\nvcc.exe" --version

# Make it visible to the later workflow steps (the build script finds it via CUDA_PATH).
$env:CUDA_PATH = $cudaPath
if ($env:GITHUB_ENV) { "CUDA_PATH=$cudaPath" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
if ($env:GITHUB_PATH) { "$cudaPath\bin" | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8 }
