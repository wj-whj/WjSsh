$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$distRoot = Join-Path $root "dist"
$dist = Join-Path $distRoot "WjSsh"
$archive = Join-Path $distRoot "WjSsh-win64.zip"
$installer = Join-Path $distRoot "WjSsh-Setup.exe"

function Find-FirstExistingPath {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Resolve-UcrtPrefix {
    $candidates = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($env:MSYSTEM_PREFIX)) {
        $candidates.Add($env:MSYSTEM_PREFIX)
    }

    if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
        $candidates.Add((Join-Path $env:RUNNER_TEMP "msys64\ucrt64"))
    }

    $candidates.Add("C:\msys64\ucrt64")
    $candidates.Add("C:\tools\msys64\ucrt64")
    $candidates.Add("D:\a\_temp\msys64\ucrt64")

    foreach ($toolName in @("cmake.exe", "windeployqt6.exe")) {
        $command = Get-Command $toolName -ErrorAction SilentlyContinue
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Path)) {
            $binDir = Split-Path -Parent $command.Path
            $prefix = Split-Path -Parent $binDir
            if (Test-Path -LiteralPath (Join-Path $prefix "bin\$toolName")) {
                $candidates.Add($prefix)
            }
        }
    }

    return Find-FirstExistingPath -Candidates $candidates.ToArray()
}

function Resolve-ToolPath {
    param(
        [string]$PreferredPath,
        [string]$CommandName
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath) -and (Test-Path -LiteralPath $PreferredPath)) {
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Path)) {
        return $command.Path
    }

    throw "Unable to locate $CommandName."
}

$ucrtPrefix = Resolve-UcrtPrefix
if ([string]::IsNullOrWhiteSpace($ucrtPrefix)) {
    throw "Unable to locate the MSYS2 UCRT64 toolchain."
}

$msys = Join-Path $ucrtPrefix "bin"
$msysRoot = Split-Path -Parent $ucrtPrefix
$env:PATH = "$msys;$(Join-Path $msysRoot 'usr\\bin');$env:PATH"
$cmakeExe = Resolve-ToolPath -PreferredPath (Join-Path $msys "cmake.exe") -CommandName "cmake.exe"
$windeployqtExe = Resolve-ToolPath -PreferredPath (Join-Path $msys "windeployqt6.exe") -CommandName "windeployqt6.exe"
$cmakePrefixPath = $ucrtPrefix.Replace("\", "/")

function New-IExpressInstaller {
    param(
        [string]$ProjectRoot,
        [string]$PayloadZip,
        [string]$OutputInstaller
    )

    $stageRoot = Join-Path $ProjectRoot "tmp\iexpress-stage"
    $sedPath = Join-Path $stageRoot "WjSsh-Setup.sed"
    $payloadName = "WjSsh-payload.zip"

    if (Test-Path $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    Copy-Item (Join-Path $ProjectRoot "installer\install.cmd") (Join-Path $stageRoot "install.cmd") -Force
    Copy-Item (Join-Path $ProjectRoot "installer\install.ps1") (Join-Path $stageRoot "install.ps1") -Force
    Copy-Item $PayloadZip (Join-Path $stageRoot $payloadName) -Force

    if (Test-Path $OutputInstaller) {
        Remove-Item -LiteralPath $OutputInstaller -Force
    }

    $sedContent = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=WjSsh has been installed to %LOCALAPPDATA%\Programs\WjSsh.
TargetName=$OutputInstaller
FriendlyName=WjSsh Setup
AppLaunched=cmd.exe /d /s /c ""install.cmd""
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[Strings]
FILE0=install.cmd
FILE1=install.ps1
FILE2=$payloadName
[SourceFiles]
SourceFiles0=$stageRoot
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@

    Set-Content -LiteralPath $sedPath -Value $sedContent -Encoding ASCII
    Start-Process -FilePath "$env:WINDIR\System32\iexpress.exe" -ArgumentList "/N", "/Q", $sedPath -Wait -NoNewWindow

    if (-not (Test-Path $OutputInstaller)) {
        throw "Installer creation failed: $OutputInstaller"
    }
}

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

& $cmakeExe -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$cmakePrefixPath
& $cmakeExe --build $build --config Release

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

& $windeployqtExe --release --compiler-runtime --no-translations --no-opengl-sw --dir $dist (Join-Path $dist "WjSsh.exe")

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
New-IExpressInstaller -ProjectRoot $root -PayloadZip $archive -OutputInstaller $installer

Write-Host "Build complete:"
Write-Host "  App: $dist"
Write-Host "  Zip: $archive"
Write-Host "  Setup: $installer"
