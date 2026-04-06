param(
    [string]$Owner = "",

    [string]$RepositoryName = "",

    [string]$Visibility = "",

    [string]$Description = "",

    [string]$CommitMessage = "Update source import",

    [string]$Token = "",

    [string]$SourceRoot = "",

    [string]$ProjectConfigPath = "",

    [string]$TokenStorePath = "",

    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Get-PlainTextFromSecureString {
    param([Security.SecureString]$SecureString)

    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureString)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

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

function Resolve-Value {
    param(
        [string]$ExplicitValue,
        [object]$StoredValue,
        [string]$DefaultValue = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitValue)) {
        return $ExplicitValue
    }

    if ($null -ne $StoredValue -and -not [string]::IsNullOrWhiteSpace([string]$StoredValue)) {
        return [string]$StoredValue
    }

    return $DefaultValue
}

function Get-ProjectConfig {
    param([string]$ConfigPathValue)

    if (-not (Test-Path -LiteralPath $ConfigPathValue)) {
        throw "GitHub publish setup was not found. Run .\setup_github_publish.ps1 first."
    }

    return Get-Content -LiteralPath $ConfigPathValue -Raw | ConvertFrom-Json
}

function Get-StoredTokenValue {
    param([string]$StorePathValue)

    if (-not (Test-Path -LiteralPath $StorePathValue)) {
        throw "A saved GitHub token was not found. Run .\setup_github_publish.ps1 first."
    }

    $secure = Get-Content -LiteralPath $StorePathValue | ConvertTo-SecureString
    return Get-PlainTextFromSecureString -SecureString $secure
}

$defaultDescription = "Modern Windows SSH and SFTP desktop client built with C++, Qt 6, and libssh."
$scriptRoot = Split-Path -Parent $PSCommandPath
$defaultSourceRoot = Resolve-SourceRoot -PathValue $SourceRoot
$defaultProjectConfigPath = Join-Path $defaultSourceRoot ".github-publish.local.json"
$defaultTokenPath = Join-Path $env:LOCALAPPDATA "WjSsh\github-publish\token.dat"
$resolvedProjectConfigPath = Resolve-OptionalPath -PathValue $ProjectConfigPath -DefaultPath $defaultProjectConfigPath
$resolvedTokenStorePath = Resolve-OptionalPath -PathValue $TokenStorePath -DefaultPath $defaultTokenPath

$projectConfig = Get-ProjectConfig -ConfigPathValue $resolvedProjectConfigPath

$effectiveSourceRoot = Resolve-Value -ExplicitValue $SourceRoot -StoredValue $projectConfig.sourceRoot -DefaultValue $defaultSourceRoot
if (-not (Test-Path -LiteralPath $effectiveSourceRoot)) {
    $effectiveSourceRoot = $defaultSourceRoot
}
$effectiveSourceRoot = Resolve-SourceRoot -PathValue $effectiveSourceRoot

$effectiveRepositoryName = Resolve-Value -ExplicitValue $RepositoryName -StoredValue $projectConfig.repositoryName -DefaultValue (Split-Path -Leaf $effectiveSourceRoot)
if ([string]::IsNullOrWhiteSpace($effectiveRepositoryName)) {
    throw "RepositoryName could not be resolved."
}

$effectiveOwner = Resolve-Value -ExplicitValue $Owner -StoredValue $projectConfig.owner
$effectiveVisibility = Resolve-Value -ExplicitValue $Visibility -StoredValue $projectConfig.visibility -DefaultValue "public"
if ($effectiveVisibility -notin @("public", "private")) {
    throw "Visibility must be either 'public' or 'private'."
}

$effectiveDescription = Resolve-Value -ExplicitValue $Description -StoredValue $projectConfig.description -DefaultValue $defaultDescription
$effectiveToken = $Token
if (-not $DryRun -and [string]::IsNullOrWhiteSpace($effectiveToken)) {
    $effectiveToken = Get-StoredTokenValue -StorePathValue $resolvedTokenStorePath
}

$publishScriptPath = Join-Path $scriptRoot "publish_to_github.ps1"
$publishParams = @{
    RepositoryName = $effectiveRepositoryName
    Visibility     = $effectiveVisibility
    Description    = $effectiveDescription
    SourceRoot     = $effectiveSourceRoot
    CommitMessage  = $CommitMessage
}

if (-not [string]::IsNullOrWhiteSpace($effectiveOwner)) {
    $publishParams.Owner = $effectiveOwner
}

if ($DryRun) {
    $publishParams.DryRun = $true
} else {
    $publishParams.Token = $effectiveToken
}

Write-Host ("Publishing {0} from {1}" -f $effectiveRepositoryName, $effectiveSourceRoot)
& $publishScriptPath @publishParams
