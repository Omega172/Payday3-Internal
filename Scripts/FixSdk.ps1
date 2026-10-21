param([string]$Sdk = "")
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Sdk) { $Sdk = Join-Path $Repo "Source\Third-Party\Dumper-7\CppSDK" }
$SdkFolder = Join-Path $Sdk "SDK"
if (-not (Test-Path $SdkFolder)) { throw "no Dumper-7 SDK folder at $SdkFolder" }

$Pattern = [regex]'(?m)^(\s*)FOptionalProperty_(\s+)(\w+);(\s*)// (0x[0-9A-Fa-f]+)\((0x[0-9A-Fa-f]+)\)'
$Utf8Bom = New-Object System.Text.UTF8Encoding($true)
$Fixed = 0
foreach ($File in Get-ChildItem $SdkFolder -Filter *.hpp) {
    $Text = [System.IO.File]::ReadAllText($File.FullName)
    if (-not $Pattern.IsMatch($Text)) { continue }
    $Count = $Pattern.Matches($Text).Count
    $Text = $Pattern.Replace($Text, {
        param($Match)
        $Declaration = "uint8 " + $Match.Groups[3].Value + "[" + $Match.Groups[6].Value + "];"
        $Match.Groups[1].Value + $Declaration.PadRight(("FOptionalProperty_" + $Match.Groups[2].Value + $Match.Groups[3].Value + ";").Length) + $Match.Groups[4].Value + "// " + $Match.Groups[5].Value + "(" + $Match.Groups[6].Value + ")"
    })
    [System.IO.File]::WriteAllText($File.FullName, $Text, $Utf8Bom)
    "{0}: {1} optional member(s) sized" -f $File.Name, $Count
    $Fixed += $Count
}
"{0} FOptionalProperty_ placeholder(s) replaced" -f $Fixed

$StructPattern = [regex]'(?m)^struct (\w+)'
$Owners = @{}
foreach ($File in Get-ChildItem $SdkFolder -Filter *_parameters.hpp) {
    foreach ($Match in $StructPattern.Matches([System.IO.File]::ReadAllText($File.FullName))) {
        if (-not $Owners.ContainsKey($Match.Groups[1].Value)) { $Owners[$Match.Groups[1].Value] = $File.Name }
    }
}

$UsePattern = [regex]'Params::(\w+)'
$IncludePattern = [regex]'(?m)^#include "(\w+_parameters\.hpp)"\r?\n'
$Added = 0
foreach ($File in Get-ChildItem $SdkFolder -Filter *_functions.cpp) {
    $Text = [System.IO.File]::ReadAllText($File.FullName)
    $Included = @{}
    $Last = $null
    foreach ($Match in $IncludePattern.Matches($Text)) { $Included[$Match.Groups[1].Value] = $true; $Last = $Match }
    if (-not $Last) { continue }
    $Missing = @($UsePattern.Matches($Text) | ForEach-Object { $Owners[$_.Groups[1].Value] } | Where-Object { $_ -and -not $Included.ContainsKey($_) } | Sort-Object -Unique)
    if ($Missing.Count -eq 0) { continue }
    $NewLine = if ($Last.Value.EndsWith("`r`n")) { "`r`n" } else { "`n" }
    $Lines = ($Missing | ForEach-Object { "#include `"$_`"$NewLine" }) -join ""
    $Text = $Text.Insert($Last.Index + $Last.Length, $Lines)
    [System.IO.File]::WriteAllText($File.FullName, $Text, $Utf8Bom)
    "{0}: added {1}" -f $File.Name, ($Missing -join ", ")
    $Added += $Missing.Count
}
"{0} missing parameter include(s) added" -f $Added
