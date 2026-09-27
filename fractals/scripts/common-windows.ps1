# Shared helpers for the Windows setup/build scripts. Dot-source this file.

function Find-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($path) { return $path.Trim() }
    return $null
}

# Prefer a tool on PATH, else the copy bundled with Visual Studio.
function Find-BuildTool([string]$name, [string]$vs) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if ($vs) {
        $bundled = @{
            cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
        }[$name]
        if ($bundled -and (Test-Path $bundled)) { return $bundled }
    }
    return $null
}

function Find-Nvcc {
    $cmd = Get-Command nvcc -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if ($env:CUDA_PATH -and (Test-Path "$env:CUDA_PATH\bin\nvcc.exe")) { return "$env:CUDA_PATH\bin\nvcc.exe" }
    return $null
}

function Get-CiCudaVersion {
    $line = Select-String -Path "$PSScriptRoot\..\cmake\versions.cmake" -Pattern 'FRACTALS_CUDA_CI_VERSION\s+([0-9.]+)'
    return $line.Matches[0].Groups[1].Value
}
