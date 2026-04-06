$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$payloadZip = Join-Path $scriptRoot "WjSsh-payload.zip"
$installRoot = Join-Path $env:LOCALAPPDATA "Programs\WjSsh"
$tempExtract = Join-Path ([IO.Path]::GetTempPath()) ("WjSshInstall-" + [guid]::NewGuid().ToString("N"))
$desktopShortcutPath = Join-Path ([Environment]::GetFolderPath("Desktop")) "WjSsh.lnk"
$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\WjSsh"
$startMenuShortcutPath = Join-Path $startMenuDir "WjSsh.lnk"

if (-not (Test-Path -LiteralPath $payloadZip)) {
    throw "Installer payload was not found: $payloadZip"
}

New-Item -ItemType Directory -Force -Path $tempExtract | Out-Null
New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
Expand-Archive -LiteralPath $payloadZip -DestinationPath $tempExtract -Force

Get-ChildItem -LiteralPath $installRoot -Force -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

Copy-Item -Path (Join-Path $tempExtract "*") -Destination $installRoot -Recurse -Force

$shell = New-Object -ComObject WScript.Shell

$desktopShortcut = $shell.CreateShortcut($desktopShortcutPath)
$desktopShortcut.TargetPath = Join-Path $installRoot "WjSsh.exe"
$desktopShortcut.WorkingDirectory = $installRoot
$desktopShortcut.IconLocation = Join-Path $installRoot "WjSsh.exe"
$desktopShortcut.Save()

New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
$startMenuShortcut = $shell.CreateShortcut($startMenuShortcutPath)
$startMenuShortcut.TargetPath = Join-Path $installRoot "WjSsh.exe"
$startMenuShortcut.WorkingDirectory = $installRoot
$startMenuShortcut.IconLocation = Join-Path $installRoot "WjSsh.exe"
$startMenuShortcut.Save()

Remove-Item -LiteralPath $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
