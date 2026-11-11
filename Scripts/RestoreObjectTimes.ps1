param(
    [Parameter(Mandatory = $true)][ValidateSet("Save", "Restore", "Packages")][string]$Action,
    [string]$Manifest = "Build\SourceManifest.txt"
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ManifestPath = if ([System.IO.Path]::IsPathRooted($Manifest)) { $Manifest } else { Join-Path $Repo $Manifest }
$Extensions = @(".cpp", ".c", ".hpp", ".h", ".hxx", ".inl", ".in", ".rc", ".lua")
$OldTime = [DateTime]::SpecifyKind([DateTime]"2001-01-01 00:00:00", [DateTimeKind]::Utc)
$PackageTag = "#packages"

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

function Get-PackageRoots {
    $Roots = @()
    $XmakeGlobal = if ($env:XMAKE_GLOBALDIR) { $env:XMAKE_GLOBALDIR } else { $env:LOCALAPPDATA }
    if ($XmakeGlobal) { $Roots += Join-Path $XmakeGlobal ".xmake\packages" }
    $Vcpkg = if ($env:VCPKG_INSTALLATION_ROOT) { $env:VCPKG_INSTALLATION_ROOT } else { $env:VCPKG_ROOT }
    if ($Vcpkg) { $Roots += Join-Path $Vcpkg "installed" }
    return @($Roots | Where-Object { Test-Path -LiteralPath $_ })
}

function Get-PackageFingerprint([string[]]$Roots) {
    $Lines = foreach ($Root in $Roots) {
        foreach ($File in (Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue)) {
            "{0}|{1}" -f $File.FullName.Substring($Root.Length), $File.Length
        }
    }
    $Text = (@($Lines) | Sort-Object) -join "`n"
    $Sha = [System.Security.Cryptography.SHA256]::Create()
    return [BitConverter]::ToString($Sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text))).Replace("-", "")
}

function Get-SavedPackageFingerprint {
    if (-not (Test-Path $ManifestPath)) { return "" }
    foreach ($Line in (Get-Content -Path $ManifestPath -Encoding utf8)) {
        if ($Line.StartsWith($PackageTag + "`t")) { return $Line.Substring($PackageTag.Length + 1) }
    }
    return ""
}

if ($Action -eq "Packages") {
    $Roots = Get-PackageRoots
    if ($Roots.Count -eq 0) {
        Write-Host "no package folders found, their file times are left alone"
        exit 0
    }
    $Saved = Get-SavedPackageFingerprint
    $Now = Get-PackageFingerprint $Roots
    if ($Saved -ne "" -and $Saved -ne $Now) {
        Write-Host "the installed packages changed since the cached build, their file times are left alone"
        exit 0
    }
    $Count = 0
    foreach ($Root in $Roots) {
        foreach ($File in (Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue)) {
            try { [System.IO.File]::SetLastWriteTimeUtc($File.FullName, $OldTime); $Count++ } catch { }
        }
    }
    Write-Host "gave $Count package files the fixed old time"
    exit 0
}

$Current = Get-SourceHashes

if ($Action -eq "Save") {
    New-Item -ItemType Directory -Force (Split-Path $ManifestPath) | Out-Null
    $Lines = @($Current.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}`t{1}" -f $_.Value, $_.Name })
    $Roots = Get-PackageRoots
    if ($Roots.Count -gt 0) { $Lines = @("{0}`t{1}" -f $PackageTag, (Get-PackageFingerprint $Roots)) + $Lines }
    $Lines | Set-Content -Path $ManifestPath -Encoding utf8
    Write-Host "saved $($Current.Count) source hashes to $ManifestPath"
    exit 0
}

if (-not (Test-Path $ManifestPath)) {
    foreach ($Path in $Current.Keys) {
        $Full = Join-Path $Repo $Path
        if (Test-Path -LiteralPath $Full) { try { [System.IO.File]::SetLastWriteTimeUtc($Full, $OldTime) } catch { } }
    }
    Write-Host "no manifest at $ManifestPath, gave all $($Current.Count) sources the fixed old time for the cold build"
    exit 0
}

$Unchanged = 0
$Changed = 0
foreach ($Line in (Get-Content -Path $ManifestPath -Encoding utf8)) {
    if ($Line.StartsWith("#")) { continue }
    $Tab = $Line.IndexOf("`t")
    if ($Tab -lt 0) { continue }
    $Hash = $Line.Substring(0, $Tab)
    $Path = $Line.Substring($Tab + 1)
    if (-not $Current.ContainsKey($Path)) { continue }
    $Full = Join-Path $Repo $Path
    if (-not (Test-Path -LiteralPath $Full)) { continue }
    if ($Current[$Path] -eq $Hash) {
        try { [System.IO.File]::SetLastWriteTimeUtc($Full, $OldTime) } catch { }
        $Unchanged++
    }
    else {
        $Changed++
    }
}
Write-Host "restored the old time on $Unchanged unchanged sources, $Changed changed sources keep their new time"
