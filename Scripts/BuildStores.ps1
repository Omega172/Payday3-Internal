param(
    [string[]]$Modes = @("release"),
    [Parameter(Mandatory)][string]$SteamSdk,
    [Parameter(Mandatory)][string]$WinGdkSdk,
    [string]$Out = "Packages"
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Out = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }))
$Stores = @(
    @{ Name = "Steam"; Variant = ""; Sdk = (Resolve-Path $SteamSdk).Path },
    @{ Name = "WinGDK"; Variant = "WinGDK"; Sdk = (Resolve-Path $WinGdkSdk).Path }
)

Push-Location $Repo
try {
    foreach ($Mode in $Modes) {
        $Folder = if ($Mode -eq "debug") { "Debug" } else { "Release" }
        $Build = Join-Path $Repo "Build\$Folder\x64"
        $Tested = $false
        foreach ($Store in $Stores) {
            Write-Host "== $Mode $($Store.Name) (SDK $($Store.Sdk))"
            xmake f -p windows -m $Mode -a x64 --proxy=version "--unreal_sdk=$($Store.Sdk)" "--variant=$($Store.Variant)" -y
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            xmake build
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            if (-not $Tested) {
                & (Join-Path $Build "OmegaWareTests\OmegaWareTests.exe")
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
                $Tested = $true
            }

            $Name = "Payday3-Internal" + $(if ($Store.Variant) { "-$($Store.Variant)" } else { "" })
            $Package = Join-Path $Out "$Mode\$($Store.Name)"
            New-Item -ItemType Directory -Force (Join-Path $Package "Proxy") | Out-Null
            Copy-Item (Join-Path $Build "OmegaWare\$Name.dll") $Package
            Copy-Item (Join-Path $Build "OmegaWareProxy\version.dll") (Join-Path $Package "Proxy")
            foreach ($Pdb in @((Join-Path $Build "OmegaWare\$Name.pdb"), (Join-Path $Build "OmegaWareProxy\version.pdb"))) {
                if (Test-Path $Pdb) {
                    $Target = if ($Pdb -like "*version.pdb") { Join-Path $Package "Proxy" } else { $Package }
                    Copy-Item $Pdb $Target
                }
            }
        }
        "Built $Mode on $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') from $(git -C $Repo rev-parse --short HEAD)" | Out-File (Join-Path $Out "$Mode\BUILD_INFO.txt") -Encoding utf8
    }
}
finally {
    Pop-Location
}