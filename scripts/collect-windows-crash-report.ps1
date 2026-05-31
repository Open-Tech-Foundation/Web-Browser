param(
  [string]$ExePath = ".\otf-browser.exe",
  [int]$Seconds = 30,
  [string[]]$ExtraArgs = @(),
  [switch]$KeepWerConfig
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

function Write-TextFile {
  param([string]$Path, [object]$Value)
  $Value | Out-File -FilePath $Path -Encoding utf8 -Width 4096
}

function Run-Capture {
  param([string]$Name, [scriptblock]$Block)
  $path = Join-Path $ReportDir $Name
  try {
    & $Block 2>&1 | Out-File -FilePath $path -Encoding utf8 -Width 4096
  } catch {
    "ERROR: $($_.Exception.Message)" | Out-File -FilePath $path -Encoding utf8
  }
}

function Copy-IfExists {
  param([string]$Path, [string]$DestinationDir)
  if (Test-Path -LiteralPath $Path) {
    Copy-Item -LiteralPath $Path -Destination $DestinationDir -Force -ErrorAction SilentlyContinue
  }
}

function Get-FileSummary {
  param([System.IO.FileInfo]$File)
  $hash = $null
  try { $hash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash } catch {}
  $version = $null
  try { $version = $File.VersionInfo.FileVersion } catch {}
  [pscustomobject]@{
    path = $File.FullName
    length = $File.Length
    sha256 = $hash
    version = $version
  }
}

$ResolvedExe = (Resolve-Path -LiteralPath $ExePath -ErrorAction Stop).Path
$ExeDir = Split-Path -Parent $ResolvedExe
$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$ReportDir = Join-Path $ExeDir "otf-crash-report-$Timestamp"
$DumpDir = Join-Path $ReportDir "dumps"
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
New-Item -ItemType Directory -Path $DumpDir -Force | Out-Null

Start-Transcript -Path (Join-Path $ReportDir "collector-transcript.txt") -Force | Out-Null

$startTime = Get-Date
$werKey = "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\otf-browser.exe"
$hadWerKey = Test-Path $werKey
$oldWerValues = @{}
try {
  if ($hadWerKey) {
    $oldWer = Get-ItemProperty -Path $werKey
    foreach ($name in @("DumpFolder", "DumpType", "DumpCount")) {
      if ($oldWer.PSObject.Properties.Name -contains $name) {
        $oldWerValues[$name] = $oldWer.PSObject.Properties[$name].Value
      }
    }
  }
  New-Item -Path $werKey -Force | Out-Null
  New-ItemProperty -Path $werKey -Name DumpFolder -Value $DumpDir -PropertyType ExpandString -Force | Out-Null
  New-ItemProperty -Path $werKey -Name DumpType -Value 2 -PropertyType DWord -Force | Out-Null
  New-ItemProperty -Path $werKey -Name DumpCount -Value 32 -PropertyType DWord -Force | Out-Null
} catch {
  Write-Warning "Failed to configure WER LocalDumps: $($_.Exception.Message)"
}

$requiredFiles = @(
  "otf-browser.exe",
  "libcef.dll",
  "chrome_elf.dll",
  "d3dcompiler_47.dll",
  "dxcompiler.dll",
  "dxil.dll",
  "libEGL.dll",
  "libGLESv2.dll",
  "vk_swiftshader.dll",
  "vk_swiftshader_icd.json",
  "vulkan-1.dll",
  "v8_context_snapshot.bin",
  "icudtl.dat",
  "resources.pak",
  "chrome_100_percent.pak",
  "chrome_200_percent.pak",
  "locales",
  "ui\index.html"
)

$summary = [ordered]@{
  generatedAt = (Get-Date).ToString("o")
  exePath = $ResolvedExe
  exeDir = $ExeDir
  seconds = $Seconds
  extraArgs = $ExtraArgs
  werDumpDir = $DumpDir
  missingRequiredFiles = @()
}

$missing = @()
foreach ($file in $requiredFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $ExeDir $file))) {
    $missing += $file
  }
}
$summary.missingRequiredFiles = $missing
$summary | ConvertTo-Json -Depth 6 | Out-File -FilePath (Join-Path $ReportDir "summary.json") -Encoding utf8

Run-Capture "systeminfo.txt" { systeminfo }
Run-Capture "driverquery.txt" { driverquery /v }
Run-Capture "computer-info.json" {
  Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber, OsArchitecture, CsManufacturer, CsModel, CsTotalPhysicalMemory | ConvertTo-Json -Depth 4
}
Run-Capture "video-controllers.json" {
  Get-CimInstance Win32_VideoController | Select-Object Name, AdapterCompatibility, DriverVersion, DriverDate, VideoProcessor, AdapterRAM, CurrentHorizontalResolution, CurrentVerticalResolution, Status | ConvertTo-Json -Depth 6
}
Run-Capture "display-devices.txt" {
  Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPClass -eq "Display" } | Select-Object Name, Manufacturer, Status, DeviceID | Format-List
}
Run-Capture "wer-localdumps-registry.txt" { reg query "HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\otf-browser.exe" /s }

$dxdiagPath = Join-Path $ReportDir "dxdiag.txt"
try {
  Start-Process -FilePath "dxdiag.exe" -ArgumentList "/t", "`"$dxdiagPath`"" -Wait -WindowStyle Hidden
} catch {
  "dxdiag failed: $($_.Exception.Message)" | Out-File -FilePath $dxdiagPath -Encoding utf8
}

$inventory = @()
Get-ChildItem -LiteralPath $ExeDir -File -ErrorAction SilentlyContinue | ForEach-Object {
  $inventory += Get-FileSummary $_
}
$inventory | ConvertTo-Json -Depth 5 | Out-File -FilePath (Join-Path $ReportDir "package-file-inventory.json") -Encoding utf8

Copy-IfExists (Join-Path $ExeDir "otf-diag.log") $ReportDir
Copy-IfExists (Join-Path $ExeDir "debug.log") $ReportDir

$processLog = Join-Path $ReportDir "process-snapshots.jsonl"
$browserArgs = @()
$browserArgs += $ExtraArgs
$proc = $null
try {
  $proc = Start-Process -FilePath $ResolvedExe -ArgumentList $browserArgs -WorkingDirectory $ExeDir -PassThru
  "started pid=$($proc.Id) at=$((Get-Date).ToString("o")) args=$($browserArgs -join ' ')" | Out-File -FilePath (Join-Path $ReportDir "run.txt") -Encoding utf8
} catch {
  "failed to start: $($_.Exception.Message)" | Out-File -FilePath (Join-Path $ReportDir "run.txt") -Encoding utf8
}

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  try {
    $snapshot = Get-CimInstance Win32_Process -Filter "name = 'otf-browser.exe'" |
      Select-Object ProcessId, ParentProcessId, CreationDate, CommandLine
    [pscustomobject]@{
      at = (Get-Date).ToString("o")
      processes = $snapshot
    } | ConvertTo-Json -Depth 8 -Compress | Out-File -FilePath $processLog -Encoding utf8 -Append
  } catch {
    [pscustomobject]@{
      at = (Get-Date).ToString("o")
      error = $_.Exception.Message
    } | ConvertTo-Json -Compress | Out-File -FilePath $processLog -Encoding utf8 -Append
  }
  if ($proc -and $proc.HasExited) { break }
  Start-Sleep -Milliseconds 500
}

Copy-IfExists (Join-Path $ExeDir "otf-diag.log") $ReportDir
Copy-IfExists (Join-Path $ExeDir "debug.log") $ReportDir

$eventStart = $startTime.AddMinutes(-5)
Run-Capture "eventlog-application-errors.json" {
  Get-WinEvent -FilterHashtable @{ LogName = "Application"; StartTime = $eventStart } |
    Where-Object {
      $_.ProviderName -in @("Application Error", "Windows Error Reporting") -and
      ($_.Message -match "otf-browser|libcef|chrome_elf")
    } |
    Select-Object TimeCreated, ProviderName, Id, LevelDisplayName, Message |
    ConvertTo-Json -Depth 8
}
Run-Capture "eventlog-application-errors.txt" {
  Get-WinEvent -FilterHashtable @{ LogName = "Application"; StartTime = $eventStart } |
    Where-Object {
      $_.ProviderName -in @("Application Error", "Windows Error Reporting") -and
      ($_.Message -match "otf-browser|libcef|chrome_elf")
    } |
    Format-List TimeCreated, ProviderName, Id, LevelDisplayName, Message
}

$werRoots = @(
  "$env:LOCALAPPDATA\Microsoft\Windows\WER\ReportArchive",
  "$env:LOCALAPPDATA\Microsoft\Windows\WER\ReportQueue",
  "$env:ProgramData\Microsoft\Windows\WER\ReportArchive",
  "$env:ProgramData\Microsoft\Windows\WER\ReportQueue"
)
$werOut = Join-Path $ReportDir "wer-reports"
New-Item -ItemType Directory -Path $werOut -Force | Out-Null
foreach ($root in $werRoots) {
  if (Test-Path -LiteralPath $root) {
    Get-ChildItem -LiteralPath $root -Recurse -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -match "otf-browser|AppCrash" } |
      ForEach-Object {
        try {
          Copy-Item -LiteralPath $_.FullName -Destination $werOut -Recurse -Force -ErrorAction SilentlyContinue
        } catch {}
      }
  }
}

if (-not $KeepWerConfig) {
  try {
    foreach ($name in @("DumpFolder", "DumpType", "DumpCount")) {
      Remove-ItemProperty -Path $werKey -Name $name -ErrorAction SilentlyContinue
    }
    if ($oldWerValues.ContainsKey("DumpFolder")) {
      New-ItemProperty -Path $werKey -Name DumpFolder -Value $oldWerValues["DumpFolder"] -PropertyType ExpandString -Force | Out-Null
    }
    if ($oldWerValues.ContainsKey("DumpType")) {
      New-ItemProperty -Path $werKey -Name DumpType -Value $oldWerValues["DumpType"] -PropertyType DWord -Force | Out-Null
    }
    if ($oldWerValues.ContainsKey("DumpCount")) {
      New-ItemProperty -Path $werKey -Name DumpCount -Value $oldWerValues["DumpCount"] -PropertyType DWord -Force | Out-Null
    }
    if (-not $hadWerKey -and $oldWerValues.Count -eq 0) {
      Remove-Item -Path $werKey -Recurse -Force -ErrorAction SilentlyContinue
    }
  } catch {}
}

$zipPath = "$ReportDir.zip"
if (Test-Path -LiteralPath $zipPath) {
  Remove-Item -LiteralPath $zipPath -Force
}
Stop-Transcript | Out-Null
Compress-Archive -Path (Join-Path $ReportDir "*") -DestinationPath $zipPath -Force

Write-Host "OTF crash report written to:"
Write-Host $ReportDir
Write-Host $zipPath
