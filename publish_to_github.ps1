param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryName,

    [string]$Owner = "",

    [string]$Description = "Modern Windows SSH and SFTP desktop client built with C++, Qt 6, and libssh.",

    [ValidateSet("public", "private")]
    [string]$Visibility = "public",

    [string]$Token = "",

    [string]$SourceRoot = "",

    [string]$CommitMessage = "Initial source import",

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

function Get-RelativeRepoPath {
    param(
        [string]$RootPath,
        [string]$FullPath
    )

    $rootFull = [IO.Path]::GetFullPath($RootPath)
    if (-not $rootFull.EndsWith([IO.Path]::DirectorySeparatorChar) -and -not $rootFull.EndsWith([IO.Path]::AltDirectorySeparatorChar)) {
        $rootFull += [IO.Path]::DirectorySeparatorChar
    }

    $fileFull = [IO.Path]::GetFullPath($FullPath)
    $rootUri = New-Object System.Uri($rootFull)
    $fileUri = New-Object System.Uri($fileFull)
    $relativeUri = $rootUri.MakeRelativeUri($fileUri)
    return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace("\", "/")
}

function Resolve-TokenValue {
    param([string]$TokenValue)

    if (-not [string]::IsNullOrWhiteSpace($TokenValue)) {
        return $TokenValue.Trim()
    }

    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
        return $env:GITHUB_TOKEN.Trim()
    }

    $secure = Read-Host "GitHub token" -AsSecureString
    $plain = Get-PlainTextFromSecureString -SecureString $secure
    if ([string]::IsNullOrWhiteSpace($plain)) {
        throw "A GitHub token is required."
    }
    return $plain.Trim()
}

function New-GitHubHeaders {
    param([string]$AccessToken)

    return @{
        Accept                 = "application/vnd.github+json"
        Authorization          = "Bearer $AccessToken"
        "X-GitHub-Api-Version" = "2022-11-28"
        "User-Agent"           = "WjSshPublisher"
    }
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
        $params.Body = ($Body | ConvertTo-Json -Depth 100)
    }

    return Invoke-RestMethod @params
}

function Try-GetRepository {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [hashtable]$Headers
    )

    try {
        return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName" -Headers $Headers
    } catch {
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode.Value__ -eq 404) {
            return $null
        }
        throw
    }
}

function Get-AuthenticatedUser {
    param([hashtable]$Headers)
    return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/user" -Headers $Headers
}

function Ensure-Repository {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$RepoDescription,
        [string]$RepoVisibility,
        [hashtable]$Headers
    )

    $existing = Try-GetRepository -RepoOwner $RepoOwner -RepoName $RepoName -Headers $Headers
    if ($null -ne $existing) {
        return $existing
    }

    $me = Get-AuthenticatedUser -Headers $Headers
    $payload = @{
        name        = $RepoName
        description = $RepoDescription
        private     = $RepoVisibility -eq "private"
        auto_init   = $true
    }

    if ($RepoOwner -eq $me.login) {
        return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/user/repos" -Headers $Headers -Body $payload
    }

    return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/orgs/$RepoOwner/repos" -Headers $Headers -Body $payload
}

function Get-IgnorePatterns {
    param([string]$RootPath)

    $patterns = New-Object System.Collections.Generic.List[string]
    $defaults = @(
        ".git/",
        "build/",
        "dist/",
        "tmp/",
        ".vs/",
        ".vscode/",
        ".idea/",
        "*.log",
        "*.tmp",
        "*.bak",
        "Thumbs.db",
        "Desktop.ini"
    )

    foreach ($pattern in $defaults) {
        $patterns.Add($pattern)
    }

    $gitignorePath = Join-Path $RootPath ".gitignore"
    if (Test-Path -LiteralPath $gitignorePath) {
        foreach ($line in Get-Content -LiteralPath $gitignorePath) {
            $trimmed = $line.Trim()
            if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#") -or $trimmed.StartsWith("!")) {
                continue
            }
            $patterns.Add($trimmed)
        }
    }

    return $patterns
}

function Test-IgnorePattern {
    param(
        [string]$RelativePath,
        [System.Collections.Generic.List[string]]$Patterns
    )

    $normalized = $RelativePath.Replace("\", "/").TrimStart("/")
    $segments = $normalized.Split("/")
    $leaf = Split-Path -Leaf $normalized

    foreach ($pattern in $Patterns) {
        $candidate = $pattern.Replace("\", "/").Trim()
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if ($candidate.EndsWith("/")) {
            $dirPattern = $candidate.TrimEnd("/")
            if ($dirPattern.Contains("/")) {
                if ($normalized -eq $dirPattern -or $normalized.StartsWith("$dirPattern/")) {
                    return $true
                }
            } else {
                foreach ($segment in $segments) {
                    if ($segment -like $dirPattern) {
                        return $true
                    }
                }
            }
            continue
        }

        if ($candidate.Contains("/")) {
            if ($normalized -like $candidate) {
                return $true
            }
            continue
        }

        if ($leaf -like $candidate) {
            return $true
        }
    }

    return $false
}

function Get-UploadFiles {
    param([string]$RootPath)

    $patterns = Get-IgnorePatterns -RootPath $RootPath
    $files = Get-ChildItem -LiteralPath $RootPath -Recurse -File

    $results = foreach ($file in $files) {
        $relative = Get-RelativeRepoPath -RootPath $RootPath -FullPath $file.FullName
        if (Test-IgnorePattern -RelativePath $relative -Patterns $patterns) {
            continue
        }

        [PSCustomObject]@{
            FullName = $file.FullName
            RepoPath = $relative
        }
    }

    return $results | Sort-Object RepoPath
}

function Get-BranchRef {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$BranchName,
        [hashtable]$Headers
    )

    return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/ref/heads/$BranchName" -Headers $Headers
}

function Get-Commit {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$CommitSha,
        [hashtable]$Headers
    )

    return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/commits/$CommitSha" -Headers $Headers
}

function New-Blob {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [byte[]]$Bytes,
        [hashtable]$Headers
    )

    $payload = @{
        content  = [Convert]::ToBase64String($Bytes)
        encoding = "base64"
    }

    return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/blobs" -Headers $Headers -Body $payload
}

function New-Tree {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$BaseTreeSha,
        [object[]]$TreeElements,
        [hashtable]$Headers
    )

    $payload = @{
        base_tree = $BaseTreeSha
        tree      = $TreeElements
    }

    return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/trees" -Headers $Headers -Body $payload
}

function New-CommitObject {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$Message,
        [string]$TreeSha,
        [string]$ParentSha,
        [hashtable]$Headers
    )

    $payload = @{
        message = $Message
        tree    = $TreeSha
        parents = @($ParentSha)
    }

    return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/commits" -Headers $Headers -Body $payload
}

function Update-BranchRef {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$BranchName,
        [string]$CommitSha,
        [hashtable]$Headers
    )

    $payload = @{
        sha   = $CommitSha
        force = $false
    }

    return Invoke-GitHubApi -Method "PATCH" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/refs/heads/$BranchName" -Headers $Headers -Body $payload
}

$resolvedSourceRoot = Resolve-SourceRoot -PathValue $SourceRoot
$files = Get-UploadFiles -RootPath $resolvedSourceRoot
if (-not $files -or $files.Count -eq 0) {
    throw "No files matched for upload."
}

if ($DryRun) {
    Write-Host "Dry run only."
    Write-Host ("Owner: {0}" -f ($(if ([string]::IsNullOrWhiteSpace($Owner)) { "<auto>" } else { $Owner })))
    Write-Host "Repository: $RepositoryName"
    Write-Host "Source root: $resolvedSourceRoot"
    Write-Host "Files to upload: $($files.Count)"
    $files | Select-Object -First 20 RepoPath | Format-Table -HideTableHeaders
    exit 0
}

$resolvedToken = Resolve-TokenValue -TokenValue $Token
$headers = New-GitHubHeaders -AccessToken $resolvedToken
$me = Get-AuthenticatedUser -Headers $headers

if ([string]::IsNullOrWhiteSpace($Owner)) {
    $Owner = $me.login
}

$repo = Ensure-Repository -RepoOwner $Owner -RepoName $RepositoryName -RepoDescription $Description -RepoVisibility $Visibility -Headers $headers
$branchName = $repo.default_branch
$branchRef = Get-BranchRef -RepoOwner $Owner -RepoName $RepositoryName -BranchName $branchName -Headers $headers
$parentSha = $branchRef.object.sha
$parentCommit = Get-Commit -RepoOwner $Owner -RepoName $RepositoryName -CommitSha $parentSha -Headers $headers
$baseTreeSha = $parentCommit.tree.sha

$treeElements = New-Object System.Collections.Generic.List[object]
$index = 0
foreach ($file in $files) {
    $index++
    Write-Host ("[{0}/{1}] {2}" -f $index, $files.Count, $file.RepoPath)
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $blob = New-Blob -RepoOwner $Owner -RepoName $RepositoryName -Bytes $bytes -Headers $headers
    $treeElements.Add(@{
        path = $file.RepoPath
        mode = "100644"
        type = "blob"
        sha  = $blob.sha
    })
}

$tree = New-Tree -RepoOwner $Owner -RepoName $RepositoryName -BaseTreeSha $baseTreeSha -TreeElements $treeElements.ToArray() -Headers $headers
$commit = New-CommitObject -RepoOwner $Owner -RepoName $RepositoryName -Message $CommitMessage -TreeSha $tree.sha -ParentSha $parentSha -Headers $headers
Update-BranchRef -RepoOwner $Owner -RepoName $RepositoryName -BranchName $branchName -CommitSha $commit.sha -Headers $headers | Out-Null

Write-Host ""
Write-Host "Repository URL: $($repo.html_url)"
Write-Host "Branch: $branchName"
Write-Host "Commit: $($commit.sha)"
Write-Host "Uploaded files: $($files.Count)"
