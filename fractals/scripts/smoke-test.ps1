# Renders tests/smoke/*.json with save_and_exit and checks the output. The exe
# is copied into an empty folder first, so this also covers first-run setup
# (FractalsData/, extracted assets, log) and, on a machine without an NVIDIA
# driver, the CPU fallback.
#   .\scripts\smoke-test.ps1 -Exe build\windows-release\bin\fractals.exe
# -SoftwareGL (CI only) puts Mesa's llvmpipe opengl32.dll next to the exe copy:
# machines without a GPU driver only have Windows' OpenGL 1.1, whose 1024x1024
# texture limit is too small for the render.
param([Parameter(Mandatory)][string]$Exe, [switch]$SoftwareGL)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."
$work = Join-Path ([IO.Path]::GetTempPath()) ("fractals-smoke-" + [guid]::NewGuid())
New-Item -ItemType Directory $work | Out-Null
Copy-Item $Exe "$work\fractals.exe"

if ($SoftwareGL) {
    $line = Select-String -Path "$root\cmake\versions.cmake" -Pattern 'FRACTALS_CI_MESA_VERSION\s+([0-9.]+)'
    $ver = $line.Matches[0].Groups[1].Value
    $archive = Join-Path ([IO.Path]::GetTempPath()) "mesa3d-$ver-release-msvc.7z"
    if (-not (Test-Path $archive)) {
        $url = "https://github.com/pal1000/mesa-dist-win/releases/download/$ver/mesa3d-$ver-release-msvc.7z"
        Write-Host "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
    }
    $mesa = "$work\mesa"
    New-Item -ItemType Directory $mesa | Out-Null
    # Windows' own tar (libarchive) reads 7z
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $mesa x64
    if ($LASTEXITCODE -ne 0) { throw "extracting $archive failed" }
    foreach ($dll in 'opengl32.dll', 'libgallium_wgl.dll', 'libglapi.dll') {
        if (Test-Path "$mesa\x64\$dll") { Copy-Item "$mesa\x64\$dll" $work }
    }
    $env:GALLIUM_DRIVER = 'llvmpipe'
    Write-Host "Using Mesa $ver llvmpipe for OpenGL"
}

$ok = $true
try {
    foreach ($key in Get-ChildItem "$root\tests\smoke\*.json") {
        $name = $key.BaseName
        Copy-Item $key.FullName "$work\$name.json"
        Write-Host "=== $name"
        $p = Start-Process "$work\fractals.exe" -ArgumentList 'save_and_exit', "$name.json", "$name.png", 'hide' `
            -WorkingDirectory $work -PassThru
        if (-not $p.WaitForExit(300000)) { $p.Kill(); Write-Host "FAIL: timed out"; $ok = $false }
        elseif ($p.ExitCode -ne 0) { Write-Host "FAIL: exit code $($p.ExitCode)"; $ok = $false }
        $png = "$work\$name.png"
        if ((Test-Path $png) -and (Get-Item $png).Length -gt 0) { Write-Host "ok   $name.png ($((Get-Item $png).Length) bytes)" }
        else { Write-Host "FAIL: no $name.png"; $ok = $false }
        if (Test-Path "$work\changed_key.json") { Write-Host "ok   changed_key.json" }
        else { Write-Host "FAIL: no changed_key.json"; $ok = $false }
        Remove-Item "$work\changed_key.json" -ErrorAction SilentlyContinue
    }

    $log = "$work\FractalsData\fractals.log"
    Select-String -Path $log -Pattern '^(CUDA|OpenGL)' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "log  $($_.Line)" }
    if (-not (Select-String -Path $log -Pattern '^CUDA' -Quiet -ErrorAction SilentlyContinue)) {
        Write-Host "FAIL: no CUDA detection line in the log"; $ok = $false
    }
    if ((Get-ChildItem "$work\FractalsData\themes" -ErrorAction SilentlyContinue).Count -gt 0) { Write-Host "ok   assets extracted" }
    else { Write-Host "FAIL: themes not extracted"; $ok = $false }

    if ($ok) { Write-Host "smoke test passed" }
    else { Write-Host "--- log"; Get-Content $log -ErrorAction SilentlyContinue }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
if (-not $ok) { exit 1 }
