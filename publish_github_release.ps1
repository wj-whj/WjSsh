param(
    [string]$Owner = "wj-whj",
    [string]$RepositoryName = "WjSsh",
    [string]$Tag = "v0.1.0",
    [string]$ReleaseName = "",
    [string]$ReleaseNotesPath = ".github\\RELEASE_TEMPLATE.md",
    [string[]]$Assets = @(
        "dist\\WjSsh-win64.zip",
        "dist\\WjSsh-Setup.exe"
    ),
    [string]$Token = ""
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

function Resolve-TokenValue {
    param([string]$TokenValue)

    if (-not [string]::IsNullOrWhiteSpace($TokenValue)) {
        return $TokenValue.Trim()
    }

    $storedTokenPath = Join-Path $env:LOCALAPPDATA "WjSsh\\github-publish\\token.dat"
    if (Test-Path -LiteralPath $storedTokenPath) {
        $secure = Get-Content -LiteralPath $storedTokenPath | ConvertTo-SecureString
        return (Get-PlainTextFromSecureString -SecureString $secure).Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
        return $env:GITHUB_TOKEN.Trim()
    }

    throw "A GitHub token is required."
}

function New-GitHubHeaders {
    param([string]$AccessToken)

    return @{
        Accept                 = "application/vnd.github+json"
        Authorization          = "Bearer $AccessToken"
        "X-GitHub-Api-Version" = "2022-11-28"
        "User-Agent"           = "WjSshReleasePublisher"
    }
}

function Get-ReleaseNotes {
    param([string]$PathValue)

    if (Test-Path -LiteralPath $PathValue) {
        return Get-Content -LiteralPath $PathValue -Raw
    }

    return "Windows release package for WjSsh."
}

function Invoke-GitHubApi {
    param(
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers,
        [object]$Body = $null
    )

    $params = @{
        Method  = $Method
        Uri     = $Uri
        Headers = $Headers
    }

    if ($null -ne $Body) {
        $params.ContentType = "application/json"
        $params.Body = ($Body | ConvertTo-Json -Depth 50)
    }

    return Invoke-RestMethod @params
}

function Invoke-GitHubBinaryUpload {
    param(
        [string]$Uri,
        [string]$FilePath,
        [hashtable]$Headers
    )

    $uploadHeaders = @{}
    foreach ($entry in $Headers.GetEnumerator()) {
        if ($entry.Key -ne "Accept") {
            $uploadHeaders[$entry.Key] = $entry.Value
        }
    }

    Invoke-RestMethod -Method POST -Uri $Uri -Headers $uploadHeaders -ContentType "application/octet-stream" -InFile $FilePath
}

function Try-GetReleaseByTag {
    param(
        [string]$OwnerValue,
        [string]$RepoValue,
        [string]$TagValue,
        [hashtable]$Headers
    )

    try {
        return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/repos/$OwnerValue/$RepoValue/releases/tags/$TagValue" -Headers $Headers
    } catch {
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode.Value__ -eq 404) {
            return $null
        }
        throw
    }
}

function Remove-ExistingAsset {
    param(
        [object]$Release,
        [string]$AssetName,
        [hashtable]$Headers
    )

    $existing = $Release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
    if ($null -ne $existing) {
        Invoke-GitHubApi -Method "DELETE" -Uri "https://api.github.com/repos/$Owner/$RepositoryName/releases/assets/$($existing.id)" -Headers $Headers | Out-Null
    }
}

$resolvedToken = Resolve-TokenValue -TokenValue $Token
$headers = New-GitHubHeaders -AccessToken $resolvedToken
$releaseTitle = if ([string]::IsNullOrWhiteSpace($ReleaseName)) { $Tag } else { $ReleaseName }
$releaseNotes = Get-ReleaseNotes -PathValue $ReleaseNotesPath

$release = Try-GetReleaseByTag -OwnerValue $Owner -RepoValue $RepositoryName -TagValue $Tag -Headers $headers
if ($null -eq $release) {
    $payload = @{
        tag_name         = $Tag
        target_commitish = "main"
        name             = $releaseTitle
        body             = $releaseNotes
        draft            = $false
        prerelease       = $false
        generate_release_notes = $false
    }
    $release = Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/repos/$Owner/$RepositoryName/releases" -Headers $headers -Body $payload
}

$uploadBase = ($release.upload_url -replace "\{.*\}$", "")
foreach ($asset in $Assets) {
    $resolvedAsset = (Resolve-Path -LiteralPath $asset).Path
    $assetName = [IO.Path]::GetFileName($resolvedAsset)
    Remove-ExistingAsset -Release $release -AssetName $assetName -Headers $headers
    $uploadUri = "${uploadBase}?name=$([System.Uri]::EscapeDataString($assetName))"
    Invoke-GitHubBinaryUpload -Uri $uploadUri -FilePath $resolvedAsset -Headers $headers | Out-Null
    Write-Host "Uploaded asset: $assetName"
}

Write-Host "Release URL: $($release.html_url)"
