param(
    [string]$Process = "PAYDAY3-Win64-Shipping",
    [int]$Seconds = 0,
    [int]$IntervalSeconds = 1,
    [string]$ModulePattern = "Payday3|OmegaWare|^version|Pranks|Spotify",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
if (-not $OutputDirectory) { $OutputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "Build\Profile" }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$csv = Join-Path $OutputDirectory "$Process-$stamp.csv"

Write-Host "waiting for $Process ..."
$target = $null
while (-not $target) {
    $target = Get-Process -Name $Process -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $target) { Start-Sleep -Seconds 1 }
}
Write-Host "watching $($target.ProcessName) pid $($target.Id), writing $csv"
Write-Host "press Ctrl+C or close the game to see the summary"

$cores = [Environment]::ProcessorCount
$modRanges = @()
$lastModuleScan = [datetime]::MinValue
$prevThreads = @{}
$prevTotal = $target.TotalProcessorTime
$prevTime = Get-Date
$samples = New-Object System.Collections.Generic.List[object]
$modThreadCpu = @{}
$started = Get-Date
$gpuAvailable = $true
$os = Get-CimInstance Win32_OperatingSystem
$totalRamMb = [math]::Round($os.TotalVisibleMemorySize / 1024, 0)

function Get-GpuSample([int]$ProcessId) {
    $result = [pscustomobject]@{ Util = 0.0; Dedicated = 0.0; Shared = 0.0; Ok = $false }
    try {
        $engines = (Get-Counter -Counter "\GPU Engine(pid_${ProcessId}_*)\Utilization Percentage" -ErrorAction Stop).CounterSamples
        $byType = @{}
        foreach ($sample in $engines) {
            if ($sample.InstanceName -match 'engtype_(.+)$') {
                $type = $Matches[1]
                if (-not $byType.ContainsKey($type)) { $byType[$type] = 0.0 }
                $byType[$type] += $sample.CookedValue
            }
        }
        foreach ($value in $byType.Values) { if ($value -gt $result.Util) { $result.Util = $value } }
        $dedicated = (Get-Counter -Counter "\GPU Process Memory(pid_${ProcessId}_*)\Dedicated Usage" -ErrorAction Stop).CounterSamples
        $shared = (Get-Counter -Counter "\GPU Process Memory(pid_${ProcessId}_*)\Shared Usage" -ErrorAction Stop).CounterSamples
        $result.Dedicated = ($dedicated | Measure-Object CookedValue -Sum).Sum / 1MB
        $result.Shared = ($shared | Measure-Object CookedValue -Sum).Sum / 1MB
        $result.Ok = $true
    } catch { }
    return $result
}

"time,cpu_percent,mod_thread_cpu_percent,private_mb,working_set_mb,handles,threads,gpu_percent,gpu_dedicated_mb,gpu_shared_mb,system_ram_used_mb,system_ram_total_mb" | Set-Content $csv

try {
    while ($true) {
        if ($target.HasExited) { break }
        if ($Seconds -gt 0 -and ((Get-Date) - $started).TotalSeconds -ge $Seconds) { break }
        Start-Sleep -Seconds $IntervalSeconds
        $target.Refresh()
        if ($target.HasExited) { break }
        $now = Get-Date
        $elapsed = ($now - $prevTime).TotalSeconds
        $total = $target.TotalProcessorTime
        $cpu = (($total - $prevTotal).TotalSeconds / $elapsed) * 100 / $cores
        $prevTotal = $total
        $prevTime = $now

        if (($now - $lastModuleScan).TotalSeconds -ge 10) {
            $lastModuleScan = $now
            $modRanges = @()
            foreach ($module in $target.Modules) {
                if ($module.ModuleName -match $ModulePattern) {
                    $modRanges += [pscustomobject]@{ Name = $module.ModuleName; Start = $module.BaseAddress.ToInt64(); End = $module.BaseAddress.ToInt64() + $module.ModuleMemorySize }
                }
            }
        }

        $modCpu = 0.0
        foreach ($thread in $target.Threads) {
            $id = $thread.Id
            $used = $thread.TotalProcessorTime
            if ($prevThreads.ContainsKey($id)) {
                $delta = ($used - $prevThreads[$id]).TotalSeconds * 100 / $elapsed / $cores
                $address = $thread.StartAddress.ToInt64()
                foreach ($range in $modRanges) {
                    if ($address -ge $range.Start -and $address -lt $range.End) {
                        $modCpu += $delta
                        if (-not $modThreadCpu.ContainsKey($range.Name)) { $modThreadCpu[$range.Name] = 0.0 }
                        $modThreadCpu[$range.Name] += $delta
                    }
                }
            }
            $prevThreads[$id] = $used
        }

        $gpu = if ($gpuAvailable) { Get-GpuSample $target.Id } else { $null }
        if ($gpu -and -not $gpu.Ok -and $samples.Count -eq 0) { $gpuAvailable = $false; Write-Host "gpu counters are not available on this machine, gpu columns will be 0" }
        if (-not $gpu) { $gpu = [pscustomobject]@{ Util = 0.0; Dedicated = 0.0; Shared = 0.0; Ok = $false } }
        $freeRamMb = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024
        $row = [pscustomobject]@{
            Time = $now
            Cpu = [math]::Round($cpu, 2)
            ModCpu = [math]::Round($modCpu, 2)
            Private = [math]::Round($target.PrivateMemorySize64 / 1MB, 1)
            Working = [math]::Round($target.WorkingSet64 / 1MB, 1)
            Handles = $target.HandleCount
            Threads = $target.Threads.Count
            Gpu = [math]::Round($gpu.Util, 1)
            GpuDedicated = [math]::Round($gpu.Dedicated, 0)
            GpuShared = [math]::Round($gpu.Shared, 0)
            RamUsed = [math]::Round($totalRamMb - $freeRamMb, 0)
        }
        $samples.Add($row)
        ("{0:o},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}" -f $row.Time, $row.Cpu, $row.ModCpu, $row.Private, $row.Working, $row.Handles, $row.Threads, $row.Gpu, $row.GpuDedicated, $row.GpuShared, $row.RamUsed, $totalRamMb) | Add-Content $csv
        Write-Host ("{0:HH:mm:ss}  cpu {1,5:N1}%  gpu {2,5:N1}%  vram {3,6:N0} MB  mod threads {4,5:N2}%  private {5,7:N0} MB  system ram {6,6:N0}/{7} MB  handles {8,5}  threads {9,4}" -f $row.Time, $row.Cpu, $row.Gpu, $row.GpuDedicated, $row.ModCpu, $row.Private, $row.RamUsed, $totalRamMb, $row.Handles, $row.Threads)
    }
}
finally {
    if ($samples.Count -gt 1) {
        $first = $samples[0]; $last = $samples[$samples.Count - 1]
        $minutes = [math]::Max(($last.Time - $first.Time).TotalMinutes, 0.01)
        $avgCpu = ($samples | Measure-Object Cpu -Average).Average
        $maxCpu = ($samples | Measure-Object Cpu -Maximum).Maximum
        $avgMod = ($samples | Measure-Object ModCpu -Average).Average
        $growth = ($last.Private - $first.Private) / $minutes
        Write-Host ""
        Write-Host "===== summary over $([math]::Round($minutes, 1)) minutes ====="
        Write-Host ("game cpu: average {0:N1}%  peak {1:N1}%" -f $avgCpu, $maxCpu)
        Write-Host ("threads started by the mod: average {0:N2}% of the machine" -f $avgMod)
        foreach ($name in $modThreadCpu.Keys) { Write-Host ("  {0}: {1:N2}% average" -f $name, ($modThreadCpu[$name] / $samples.Count)) }
        $avgGpu = ($samples | Measure-Object Gpu -Average).Average
        $maxGpu = ($samples | Measure-Object Gpu -Maximum).Maximum
        Write-Host ("game gpu: average {0:N1}%  peak {1:N1}%  dedicated vram {2:N0} MB to {3:N0} MB  shared {4:N0} MB to {5:N0} MB" -f $avgGpu, $maxGpu, $first.GpuDedicated, $last.GpuDedicated, $first.GpuShared, $last.GpuShared)
        Write-Host ("system ram in use: {0:N0} MB to {1:N0} MB of {2:N0} MB" -f $first.RamUsed, $last.RamUsed, $totalRamMb)
        Write-Host ("private memory: {0:N0} MB to {1:N0} MB ({2:+0.0;-0.0} MB per minute)" -f $first.Private, $last.Private, $growth)
        Write-Host ("handles: {0} to {1}   threads: {2} to {3}" -f $first.Handles, $last.Handles, $first.Threads, $last.Threads)
        if ($growth -gt 50) { Write-Host "verdict: memory grows fast, a leak is likely, compare a run without the mod" }
        elseif (($last.Handles - $first.Handles) -gt 500) { Write-Host "verdict: handles keep growing, a handle leak is likely" }
        elseif ($avgMod -gt 1.0) { Write-Host "verdict: the mod's own threads use real CPU, check the profiler page and log" }
        else { Write-Host "verdict: no leak and the mod's own threads are cheap; slowdowns would come from work inside game threads, see the profiler lines in the log" }
        Write-Host "csv: $csv"
    }
}
