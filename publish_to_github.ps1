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
        [object]$Body = $null,
        [int]$MaxAttempts = 4,
        [switch]$RetryOnNotFound
    )

    $attempt = 0
    do {
        $attempt++
        $params = @{
            Method  = $Method
            Uri     = $Uri
            Headers = $Headers
        }

        if ($null -ne $Body) {
            $params.ContentType = "application/json"
            $params.Body = ($Body | ConvertTo-Json -Depth 100)
        }

        try {
            return Invoke-RestMethod @params
        } catch {
            $response = $_.Exception.Response
            $statusCode = $null
            $responseBody = $null

            if ($null -ne $response) {
                $statusCode = [int]$response.StatusCode
                try {
                    $stream = $response.GetResponseStream()
                    if ($null -ne $stream) {
                        $reader = New-Object IO.StreamReader($stream)
                        $responseBody = $reader.ReadToEnd()
                        $reader.Dispose()
                    }
                } catch {
                }
            }

            $retryableStatusCodes = @(400, 408, 409, 429, 500, 502, 503, 504)
            if ($RetryOnNotFound) {
                $retryableStatusCodes += 404
            }

            $shouldRetry = $attempt -lt $MaxAttempts -and $statusCode -in $retryableStatusCodes
            if ($shouldRetry) {
                Start-Sleep -Seconds ([Math]::Min(2 * $attempt, 8))
                continue
            }

            if (-not [string]::IsNullOrWhiteSpace($responseBody)) {
                throw "GitHub API $Method $Uri failed with status $statusCode. Response: $responseBody"
            }

            throw
        }
    } while ($attempt -lt $MaxAttempts)
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
        tree = $TreeElements
    }

    if (-not [string]::IsNullOrWhiteSpace($BaseTreeSha)) {
        $payload.base_tree = $BaseTreeSha
    }

    return Invoke-GitHubApi -Method "POST" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/trees" -Headers $Headers -Body $payload
}

function Get-Tree {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$TreeSha,
        [hashtable]$Headers
    )

    return Invoke-GitHubApi -Method "GET" -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/git/trees/$TreeSha" -Headers $Headers
}

function Add-TreeFile {
    param(
        [hashtable]$Node,
        [string[]]$Segments,
        [int]$Index,
        [string]$BlobSha
    )

    if ($Index -eq ($Segments.Length - 1)) {
        $Node.Files[$Segments[$Index]] = $BlobSha
        return
    }

    $segment = $Segments[$Index]
    if (-not $Node.Directories.ContainsKey($segment)) {
        $Node.Directories[$segment] = @{
            Files       = @{}
            Directories = @{}
        }
    }

    Add-TreeFile -Node $Node.Directories[$segment] -Segments $Segments -Index ($Index + 1) -BlobSha $BlobSha
}

function Resolve-ExistingTreeSha {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$DirectoryPath,
        [string]$RootTreeSha,
        [hashtable]$Headers,
        [hashtable]$ExistingTreeCache,
        [hashtable]$FetchedTreeCache
    )

    if ($ExistingTreeCache.ContainsKey($DirectoryPath)) {
        return $ExistingTreeCache[$DirectoryPath]
    }

    if ([string]::IsNullOrWhiteSpace($DirectoryPath)) {
        $ExistingTreeCache[$DirectoryPath] = $RootTreeSha
        return $RootTreeSha
    }

    $normalizedPath = $DirectoryPath.Replace("\", "/").Trim("/")
    $lastSlash = $normalizedPath.LastIndexOf("/")
    if ($lastSlash -ge 0) {
        $parentPath = $normalizedPath.Substring(0, $lastSlash)
        $segment = $normalizedPath.Substring($lastSlash + 1)
    } else {
        $parentPath = ""
        $segment = $normalizedPath
    }

    $parentTreeSha = Resolve-ExistingTreeSha -RepoOwner $RepoOwner -RepoName $RepoName -DirectoryPath $parentPath -RootTreeSha $RootTreeSha -Headers $Headers -ExistingTreeCache $ExistingTreeCache -FetchedTreeCache $FetchedTreeCache
    if ([string]::IsNullOrWhiteSpace($parentTreeSha)) {
        $ExistingTreeCache[$DirectoryPath] = $null
        return $null
    }

    if (-not $FetchedTreeCache.ContainsKey($parentTreeSha)) {
        $FetchedTreeCache[$parentTreeSha] = Get-Tree -RepoOwner $RepoOwner -RepoName $RepoName -TreeSha $parentTreeSha -Headers $Headers
    }

    $parentTree = $FetchedTreeCache[$parentTreeSha]
    $match = $parentTree.tree | Where-Object { $_.type -eq "tree" -and $_.path -eq $segment } | Select-Object -First 1
    if ($null -ne $match) {
        $ExistingTreeCache[$DirectoryPath] = $match.sha
        return $match.sha
    }

    $ExistingTreeCache[$DirectoryPath] = $null
    return $null
}

function New-NestedTree {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [hashtable]$Node,
        [string]$DirectoryPath,
        [string]$RootTreeSha,
        [hashtable]$Headers,
        [hashtable]$ExistingTreeCache,
        [hashtable]$FetchedTreeCache
    )

    $treeElements = New-Object System.Collections.Generic.List[object]

    foreach ($fileName in ($Node.Files.Keys | Sort-Object)) {
        $treeElements.Add(@{
            path = $fileName
            mode = "100644"
            type = "blob"
            sha  = $Node.Files[$fileName]
        })
    }

    foreach ($directoryName in ($Node.Directories.Keys | Sort-Object)) {
        $childPath = if ([string]::IsNullOrWhiteSpace($DirectoryPath)) { $directoryName } else { "$DirectoryPath/$directoryName" }
        $childTreeSha = New-NestedTree -RepoOwner $RepoOwner -RepoName $RepoName -Node $Node.Directories[$directoryName] -DirectoryPath $childPath -RootTreeSha $RootTreeSha -Headers $Headers -ExistingTreeCache $ExistingTreeCache -FetchedTreeCache $FetchedTreeCache
        $treeElements.Add(@{
            path = $directoryName
            mode = "040000"
            type = "tree"
            sha  = $childTreeSha
        })
    }

    $baseTreeShaForNode = Resolve-ExistingTreeSha -RepoOwner $RepoOwner -RepoName $RepoName -DirectoryPath $DirectoryPath -RootTreeSha $RootTreeSha -Headers $Headers -ExistingTreeCache $ExistingTreeCache -FetchedTreeCache $FetchedTreeCache
    $tree = New-Tree -RepoOwner $RepoOwner -RepoName $RepoName -BaseTreeSha $baseTreeShaForNode -TreeElements $treeElements.ToArray() -Headers $Headers
    $ExistingTreeCache[$DirectoryPath] = $tree.sha
    $FetchedTreeCache[$tree.sha] = $tree
    return $tree.sha
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

    $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/git/refs/heads/$BranchName"
    for ($attempt = 1; $attempt -le 6; $attempt++) {
        try {
            return Invoke-RestMethod -Method "PATCH" -Uri $uri -Headers $Headers -ContentType "application/json" -Body ($payload | ConvertTo-Json -Depth 20)
        } catch {
            $statusCode = $null
            if ($_.Exception.Response) {
                try {
                    $statusCode = [int]$_.Exception.Response.StatusCode
                } catch {
                }
            }

            if ($attempt -lt 6 -and ($statusCode -eq 404 -or $statusCode -eq 409 -or $statusCode -eq 422)) {
                Start-Sleep -Seconds ([Math]::Min($attempt * 2, 10))
                continue
            }

            throw
        }
    }
}

function Get-EncodedRepositoryPath {
    param([string]$RepoPath)

    $segments = $RepoPath.Replace("\", "/").Split("/")
    $encodedSegments = foreach ($segment in $segments) {
        [System.Uri]::EscapeDataString($segment)
    }
    return ($encodedSegments -join "/")
}

function Try-GetContentItem {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$RepoPath,
        [string]$BranchName,
        [hashtable]$Headers
    )

    $encodedPath = Get-EncodedRepositoryPath -RepoPath $RepoPath
    $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/contents/${encodedPath}?ref=$([System.Uri]::EscapeDataString($BranchName))"
    try {
        return Invoke-RestMethod -Method "GET" -Uri $uri -Headers $Headers
    } catch {
        if ($_.Exception.Response) {
            try {
                if ([int]$_.Exception.Response.StatusCode -eq 404) {
                    return $null
                }
            } catch {
            }
        }
        throw
    }
}

function Upsert-ContentFile {
    param(
        [string]$RepoOwner,
        [string]$RepoName,
        [string]$RepoPath,
        [byte[]]$Bytes,
        [string]$BranchName,
        [string]$Message,
        [hashtable]$Headers
    )

    $existingItem = Try-GetContentItem -RepoOwner $RepoOwner -RepoName $RepoName -RepoPath $RepoPath -BranchName $BranchName -Headers $Headers
    $payload = @{
        message = $Message
        content = [Convert]::ToBase64String($Bytes)
        branch  = $BranchName
    }

    if ($null -ne $existingItem -and -not [string]::IsNullOrWhiteSpace([string]$existingItem.sha)) {
        $payload.sha = [string]$existingItem.sha
    }

    $encodedPath = Get-EncodedRepositoryPath -RepoPath $RepoPath
    $uri = "https://api.github.com/repos/$RepoOwner/$RepoName/contents/$encodedPath"
    $body = $payload | ConvertTo-Json -Depth 20

    try {
        return Invoke-RestMethod -Method "PUT" -Uri $uri -Headers $Headers -ContentType "application/json" -Body $body
    } catch {
        $statusCode = $null
        $responseText = ""
        if ($_.Exception.Response) {
            try {
                $statusCode = [int]$_.Exception.Response.StatusCode
                $stream = $_.Exception.Response.GetResponseStream()
                if ($stream) {
                    $reader = New-Object IO.StreamReader($stream)
                    $responseText = $reader.ReadToEnd()
                    $reader.Dispose()
                }
            } catch {
            }
        }

        $needsShaRetry = $statusCode -eq 422 -and $responseText -like '*"sha" wasn''t supplied*'
        if ($needsShaRetry) {
            $refreshedItem = Try-GetContentItem -RepoOwner $RepoOwner -RepoName $RepoName -RepoPath $RepoPath -BranchName $BranchName -Headers $Headers
            if ($null -ne $refreshedItem -and -not [string]::IsNullOrWhiteSpace([string]$refreshedItem.sha)) {
                $payload.sha = [string]$refreshedItem.sha
                return Invoke-RestMethod -Method "PUT" -Uri $uri -Headers $Headers -ContentType "application/json" -Body ($payload | ConvertTo-Json -Depth 20)
            }
        }

        throw
    }
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

$rootNode = @{
    Files       = @{}
    Directories = @{}
}
$index = 0
foreach ($file in $files) {
    $index++
    Write-Host ("[{0}/{1}] {2}" -f $index, $files.Count, $file.RepoPath)
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $blob = New-Blob -RepoOwner $Owner -RepoName $RepositoryName -Bytes $bytes -Headers $headers
    $segments = $file.RepoPath.Split("/")
    Add-TreeFile -Node $rootNode -Segments $segments -Index 0 -BlobSha $blob.sha
}

$existingTreeCache = @{}
$fetchedTreeCache = @{}
try {
    $treeSha = New-NestedTree -RepoOwner $Owner -RepoName $RepositoryName -Node $rootNode -DirectoryPath "" -RootTreeSha $baseTreeSha -Headers $headers -ExistingTreeCache $existingTreeCache -FetchedTreeCache $fetchedTreeCache
    $tree = @{ sha = $treeSha }
    $commit = New-CommitObject -RepoOwner $Owner -RepoName $RepositoryName -Message $CommitMessage -TreeSha $tree.sha -ParentSha $parentSha -Headers $headers
    Update-BranchRef -RepoOwner $Owner -RepoName $RepositoryName -BranchName $branchName -CommitSha $commit.sha -Headers $headers | Out-Null

    Write-Host ""
    Write-Host "Repository URL: $($repo.html_url)"
    Write-Host "Branch: $branchName"
    Write-Host "Commit: $($commit.sha)"
    Write-Host "Uploaded files: $($files.Count)"
    exit 0
} catch {
    Write-Warning "Git database upload failed. Falling back to GitHub contents API."
    Write-Warning $_
}

$index = 0
foreach ($file in $files) {
    $index++
    if ($file.RepoPath -like ".github/workflows/*") {
        Write-Warning ("Skipping {0} in contents API fallback because GitHub workflow updates require a token with workflow permission." -f $file.RepoPath)
        continue
    }
    Write-Host ("[{0}/{1}] {2} (contents API)" -f $index, $files.Count, $file.RepoPath)
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    Upsert-ContentFile -RepoOwner $Owner -RepoName $RepositoryName -RepoPath $file.RepoPath -Bytes $bytes -BranchName $branchName -Message $CommitMessage -Headers $headers | Out-Null
}

Write-Host ""
Write-Host "Repository URL: $($repo.html_url)"
Write-Host "Branch: $branchName"
Write-Host "Uploaded files: $($files.Count)"
