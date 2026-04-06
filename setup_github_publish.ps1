param(
    [string]$Owner = "",

    [string]$RepositoryName = "",

    [string]$Visibility = "",

    [string]$Description = "",

    [string]$Token = "",

    [string]$SourceRoot = "",

    [string]$ProjectConfigPath = "",

    [string]$TokenStorePath = "",

    [switch]$UpdateToken
)

$ErrorActionPreference = "Stop"

function Resolve-SourceRoot {
    param([string]$PathValue)

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return Split-Path -Parent $PSCommandPath
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Resolve-OptionalPath {
    param(
        [string]$PathValue,
        [string]$DefaultPath
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $DefaultPath
    }

    if ([IO.Path]::IsPathRooted($PathValue)) {
        return [IO.Path]::GetFullPath($PathValue)
    }

    return [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $PathValue))
}

function Ensure-ParentDirectory {
    param([string]$PathValue)

    $parent = Split-Path -Parent $PathValue
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Get-ExistingProjectConfig {
    param([string]$ConfigPathValue)

    if (-not (Test-Path -LiteralPath $ConfigPathValue)) {
        return $null
    }

    return Get-Content -LiteralPath $ConfigPathValue -Raw | ConvertFrom-Json
}

function Save-EncryptedToken {
    param(
        [Security.SecureString]$SecureToken,
        [string]$StorePathValue
    )

    Ensure-ParentDirectory -PathValue $StorePathValue
    $SecureToken | ConvertFrom-SecureString | Set-Content -LiteralPath $StorePathValue -Encoding UTF8
}

function Resolve-TokenSecureString {
    param(
        [string]$TokenValue,
        [string]$StorePathValue,
        [switch]$ForcePrompt
    )

    if (-not [string]::IsNullOrWhiteSpace($TokenValue)) {
        return ConvertTo-SecureString -String $TokenValue -AsPlainText -Force
    }

    if ((Test-Path -LiteralPath $StorePathValue) -and -not $ForcePrompt) {
        return $null
    }

    $secure = Read-Host "GitHub token" -AsSecureString
    if ($secure.Length -eq 0) {
        throw "A GitHub token is required."
    }
    return $secure
}

function Resolve-Value {
    param(
        [string]$ExplicitValue,
        [object]$ExistingValue,
        [string]$DefaultValue = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitValue)) {
        return $ExplicitValue
    }

    if ($null -ne $ExistingValue -and -not [string]::IsNullOrWhiteSpace([string]$ExistingValue)) {
        return [string]$ExistingValue
    }

    return $DefaultValue
}

$defaultDescription = "Modern Windows SSH and SFTP desktop client built with C++, Qt 6, and libssh."
$resolvedSourceRoot = Resolve-SourceRoot -PathValue $SourceRoot
$defaultConfigPath = Join-Path $resolvedSourceRoot ".github-publish.local.json"
$defaultTokenPath = Join-Path $env:LOCALAPPDATA "WjSsh\github-publish\token.dat"
$resolvedProjectConfigPath = Resolve-OptionalPath -PathValue $ProjectConfigPath -DefaultPath $defaultConfigPath
$resolvedTokenStorePath = Resolve-OptionalPath -PathValue $TokenStorePath -DefaultPath $defaultTokenPath

$existingConfig = Get-ExistingProjectConfig -ConfigPathValue $resolvedProjectConfigPath
$effectiveVisibility = Resolve-Value -ExplicitValue $Visibility -ExistingValue $existingConfig.visibility -DefaultValue "public"
if ($effectiveVisibility -notin @("public", "private")) {
    throw "Visibility must be either 'public' or 'private'."
}

$effectiveRepositoryName = Resolve-Value -ExplicitValue $RepositoryName -ExistingValue $existingConfig.repositoryName -DefaultValue (Split-Path -Leaf $resolvedSourceRoot)
$effectiveOwner = Resolve-Value -ExplicitValue $Owner -ExistingValue $existingConfig.owner
$effectiveDescription = Resolve-Value -ExplicitValue $Description -ExistingValue $existingConfig.description -DefaultValue $defaultDescription

$tokenSecure = Resolve-TokenSecureString -TokenValue $Token -StorePathValue $resolvedTokenStorePath -ForcePrompt:$UpdateToken
if ($null -ne $tokenSecure) {
    Save-EncryptedToken -SecureToken $tokenSecure -StorePathValue $resolvedTokenStorePath
}

$projectConfig = [ordered]@{
    owner          = $effectiveOwner
    repositoryName = $effectiveRepositoryName
    visibility     = $effectiveVisibility
    description    = $effectiveDescription
    sourceRoot     = $resolvedSourceRoot
    updatedAt      = (Get-Date).ToString("s")
}

Ensure-ParentDirectory -PathValue $resolvedProjectConfigPath
$projectConfig | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $resolvedProjectConfigPath -Encoding UTF8

Write-Host "GitHub publish setup saved."
Write-Host "Project config: $resolvedProjectConfigPath"
Write-Host "Token store: $resolvedTokenStorePath"
Write-Host ""
Write-Host "Next time you can publish with:"
Write-Host ".\quick_publish.ps1"
