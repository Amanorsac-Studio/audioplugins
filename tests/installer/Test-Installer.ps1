# Packages one bundle and proves every install path against the real payload,
# inside a throwaway folder so no system location is touched:
#   A. the silent path, with the optional second plug-in folder
#   B. a clean uninstall that leaves somebody else's plug-in alone
#   C. the elevated-worker path the wizard uses, installing VST3 only
param(
    [Parameter(Mandatory = $true)][string]$ProductName,
    [Parameter(Mandatory = $true)][string[]]$Targets,
    [string]$Version = "1.0.0",
    [string]$BuildDir = "build/win-x64",
    # Re-test an installer that is already in dist/ without repackaging it.
    [switch]$SkipBuild
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$failures = 0
function Check($ok, $what) { if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:failures++ } }

"== $ProductName installer =="
$log = if ($SkipBuild) { @() } else { & (Join-Path $root "installer/build-windows-installer.ps1") -Version $Version -BuildDir $BuildDir `
          -ProductName $ProductName -Targets $Targets -AllowUnarmed 2>&1 }
$exeName = ($ProductName -replace "\s+", "") + "-$Version-Windows.exe"
$exe = Join-Path $root "dist/$exeName"
Check (Test-Path $exe) "one file, named to the standard: $exeName"
if (-not (Test-Path $exe)) { $log | Select-Object -Last 12; exit 1 }

$t = Join-Path $env:TEMP ("amanorsac_pkg_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force (Join-Path $t "vst3/SOMEONE ELSE.vst3") | Out-Null
Set-Content (Join-Path $t "vst3/SOMEONE ELSE.vst3/keep.txt") "must survive"
$count = $Targets.Count

# ---- A
$p = Start-Process $exe -Wait -PassThru -ArgumentList @("--quiet", "--no-shortcuts", "--no-registry",
        "--vst3-root", "`"$t\vst3`"", "--install-root", "`"$t\app`"", "--extra-vst3", "`"$t\mirror`"")
Check ($p.ExitCode -eq 0) "silent install exits cleanly"
Check (((Get-ChildItem "$t\vst3" -Directory -Filter *.vst3).Count - 1) -eq $count) "$count VST3 bundles in the plug-in folder"
Check ((Get-ChildItem "$t\mirror" -Directory -Filter *.vst3 -ErrorAction SilentlyContinue).Count -eq $count) "$count copies in the optional second folder"
Check ((Get-ChildItem "$t\app" -Filter *.exe).Count -eq $count) "$count standalone applications"
Check (Test-Path "$t\app\README.txt") "README.txt installed, plain text"
Check ((Get-Content "$t\app\README.txt").Count -le 60) "README is one page ($((Get-Content "$t\app\README.txt").Count) lines)"
Check (Test-Path "$t\app\product.ico") "product icon installed for Apps & Features"
$bundle = Get-ChildItem "$t\vst3" -Directory -Filter *.vst3 | Where-Object Name -ne "SOMEONE ELSE.vst3" | Select-Object -First 1
Check (Test-Path (Join-Path $bundle.FullName "Contents\x86_64-win")) "VST3 installed as a bundle folder, not a bare file"

# ---- B
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$t\app\uninstall.ps1" -InstallRoot "$t\app" -NoRegistry -Quiet | Out-Null
$left = @(Get-ChildItem "$t\vst3" -Directory | ForEach-Object { $_.Name })   # always a list, even for one item
Check ($left.Count -eq 1 -and $left[0] -eq "SOMEONE ELSE.vst3") "uninstall removes only its own plug-ins (left: $($left -join ', '))"
Check (Test-Path "$t\vst3\SOMEONE ELSE.vst3\keep.txt") "another maker's plug-in is untouched"
Check ((Get-ChildItem "$t\mirror" -Directory -ErrorAction SilentlyContinue).Count -eq 0) "the second folder's copies are removed too"
Check (-not (Test-Path "$t\app")) "the product folder is gone"

# ---- C
$plan = @{ InstallVst3 = $true; InstallApps = $false; Vst3Root = "$t\w-vst3"; AppRoot = "$t\w-app"; ExtraVst3Root = ""
           Shortcuts = $false; Register = $false; ProgressFile = "$t\progress.txt"; ResultFile = "$t\result.txt" }
Set-Content "$t\plan.json" ($plan | ConvertTo-Json) -Encoding utf8
$p = Start-Process $exe -Wait -PassThru -ArgumentList @("--apply", "`"$t\plan.json`"")
Check ($p.ExitCode -eq 0 -and (Get-Content "$t\result.txt" -TotalCount 1) -eq "ok") "the wizard's worker reports success"
Check ((Get-Content "$t\progress.txt") -like "100|*") "progress reaches 100 %"
Check ((Get-ChildItem "$t\w-vst3" -Directory -Filter *.vst3).Count -eq $count) "VST3-only choice installs the plug-ins"
Check ((Get-ChildItem "$t\w-app" -Filter *.exe -ErrorAction SilentlyContinue).Count -eq 0) "and installs no application the buyer did not ask for"

Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue
"{0}: {1} installer, {2} failure(s)" -f $(if ($failures -eq 0) { "PASS" } else { "FAIL" }), $ProductName, $failures
exit $failures
