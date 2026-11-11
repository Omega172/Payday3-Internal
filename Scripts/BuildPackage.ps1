param(
    [string[]]$Modes = @("release"),
    [string]$Out = "Packages"
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Out = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }))

Push-Location $Repo
try {
    foreach ($Mode in $Modes) {
        $Folder = if ($Mode -eq "debug") { "Debug" } else { "Release" }
        $Build = Join-Path $Repo "Build\$Folder\x64"
        Write-Host "== $Mode"
        $ConfigureArguments = @("f", "-p", "windows", "-m", $Mode, "-a", "x64", "--proxy=version", "-y")
        if ($env:OMEGAWARE_DIAGNOSE_BUILD) { $ConfigureArguments += "--policies=diagnosis.check_build_deps:y" }
        xmake @ConfigureArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $PSScriptRoot "RestoreObjectTimes.ps1") -Action Packages
        xmake build -v
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $Build "OmegaWareTests\OmegaWareTests.exe")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        $Package = Join-Path $Out $Mode
        New-Item -ItemType Directory -Force (Join-Path $Package "Proxy") | Out-Null
        Copy-Item (Join-Path $Build "OmegaWare\Payday3-Internal.dll") $Package
        Copy-Item (Join-Path $Build "OmegaWareProxy\version.dll") (Join-Path $Package "Proxy")
        foreach ($Pdb in @((Join-Path $Build "OmegaWare\Payday3-Internal.pdb"), (Join-Path $Build "OmegaWareProxy\version.pdb"))) {
            if (Test-Path $Pdb) {
                $Target = if ($Pdb -like "*version.pdb") { Join-Path $Package "Proxy" } else { $Package }
                Copy-Item $Pdb $Target
            }
        }
        "Built $Mode on $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') from $(git -C $Repo rev-parse --short HEAD)" | Out-File (Join-Path $Package "BUILD_INFO.txt") -Encoding utf8
    }
}
finally {
    Pop-Location
}
