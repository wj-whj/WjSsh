$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$distRoot = Join-Path $root "dist"
$dist = Join-Path $distRoot "WjSsh"
$archive = Join-Path $distRoot "WjSsh-win64.zip"
$msys = "C:\msys64\ucrt64\bin"
$env:PATH = "$msys;C:\msys64\usr\bin;$env:PATH"

$runtimeLibs = @(
    "libssh.dll",
    "libcrypto-3-x64.dll",
    "zlib1.dll",
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll",
    "libstdc++-6.dll",
    "libb2-1.dll",
    "libdouble-conversion.dll",
    "libicuin78.dll",
    "libicuuc78.dll",
    "libicudt78.dll",
    "libpcre2-16-0.dll",
    "libzstd.dll",
    "libfreetype-6.dll",
    "libharfbuzz-0.dll",
    "libmd4c.dll",
    "libpng16-16.dll",
    "libbrotlidec.dll",
    "libbrotlicommon.dll",
    "libglib-2.0-0.dll",
    "libgraphite2.dll",
    "libbz2-1.dll",
    "libintl-8.dll",
    "libiconv-2.dll",
    "libpcre2-8-0.dll"
)

& "$msys\cmake.exe" -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64"
& "$msys\cmake.exe" --build $build --config Release

Get-Process WjSsh -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$dist*" } |
    Stop-Process -Force -ErrorAction SilentlyContinue

if (Test-Path $dist) {
    $removed = $false
    for ($attempt = 1; $attempt -le 5 -and -not $removed; $attempt++) {
        try {
            Remove-Item -Recurse -Force $dist
            $removed = $true
        } catch {
            if ($attempt -eq 5) {
                throw
            }
            Start-Sleep -Seconds 2
        }
    }
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item (Join-Path $build "WjSsh.exe") $dist -Force

& "$msys\windeployqt6.exe" --release --compiler-runtime --no-translations --no-opengl-sw --dir $dist (Join-Path $dist "WjSsh.exe")

foreach ($lib in $runtimeLibs) {
    Copy-Item (Join-Path $msys $lib) $dist -Force
}

$check = Start-Process -FilePath (Join-Path $dist "WjSsh.exe") -ArgumentList "--self-check" -Wait -PassThru
if ($check.ExitCode -ne 0) {
    throw "Self-check failed with exit code $($check.ExitCode)"
}

if (Test-Path $archive) {
    Remove-Item -Force $archive
}

Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $archive -Force

Write-Host "Build complete:"
Write-Host "  App: $dist"
Write-Host "  Zip: $archive"
