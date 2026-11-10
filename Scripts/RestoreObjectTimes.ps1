param(
    [Parameter(Mandatory = $true)][ValidateSet("Save", "Restore")][string]$Action,
    [string]$Manifest = "Build\SourceManifest.txt"
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ManifestPath = if ([System.IO.Path]::IsPathRooted($Manifest)) { $Manifest } else { Join-Path $Repo $Manifest }
$Extensions = @(".cpp", ".c", ".hpp", ".h", ".hxx", ".inl", ".in", ".rc", ".lua")
$OldTime = [DateTime]::SpecifyKind([DateTime]"2001-01-01 00:00:00", [DateTimeKind]::Utc)

function Get-SourceHashes {
    $Hashes = @{}
    foreach ($Line in (git -C $Repo ls-files -s --recurse-submodules)) {
        $Tab = $Line.IndexOf("`t")
        if ($Tab -lt 0) { continue }
        $Path = $Line.Substring($Tab + 1)
        if ($Extensions -notcontains [System.IO.Path]::GetExtension($Path).ToLowerInvariant()) { continue }
        $Parts = $Line.Substring(0, $Tab).Split(" ")
        $Hashes[$Path] = $Parts[1]
    }
    return $Hashes
}

$Current = Get-SourceHashes

if ($Action -eq "Save") {
    New-Item -ItemType Directory -Force (Split-Path $ManifestPath) | Out-Null
    $Current.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}`t{1}" -f $_.Value, $_.Name } | Set-Content -Path $ManifestPath -Encoding utf8
    Write-Host "saved $($Current.Count) source hashes to $ManifestPath"
    exit 0
}

if (-not (Test-Path $ManifestPath)) {
    foreach ($Path in $Current.Keys) {
        $Full = Join-Path $Repo $Path
        if (Test-Path -LiteralPath $Full) { [System.IO.File]::SetLastWriteTimeUtc($Full, $OldTime) }
    }
    Write-Host "no manifest at $ManifestPath, gave all $($Current.Count) sources the fixed old time for the cold build"
    exit 0
}

$Unchanged = 0
$Changed = 0
foreach ($Line in (Get-Content -Path $ManifestPath -Encoding utf8)) {
    $Tab = $Line.IndexOf("`t")
    if ($Tab -lt 0) { continue }
    $Hash = $Line.Substring(0, $Tab)
    $Path = $Line.Substring($Tab + 1)
    if (-not $Current.ContainsKey($Path)) { continue }
    $Full = Join-Path $Repo $Path
    if (-not (Test-Path -LiteralPath $Full)) { continue }
    if ($Current[$Path] -eq $Hash) {
        [System.IO.File]::SetLastWriteTimeUtc($Full, $OldTime)
        $Unchanged++
    }
    else {
        $Changed++
    }
}
Write-Host "restored the old time on $Unchanged unchanged sources, $Changed changed sources keep their new time"
