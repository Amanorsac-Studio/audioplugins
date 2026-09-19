<#
    Packages an Amanorsac product or bundle into one Windows installer, to the
    Amanorsac Studio Installer & Packaging Standard:

      - one .exe and nothing to unpack                               (P1, P2)
      - a branded five-page wizard with selectable components,
        full paths and an optional second plug-in folder             (P11-P18)
      - the standard system folders                                  (3.1)
      - Apps & Features registration and a clean uninstall           (P23, P25)
      - a one-page README.txt                                        (P26, P27)
      - <Product>-<version>-Windows.exe                              (P28)

    Signing (P31) is not done here: there is no Authenticode certificate in
    the organisation secrets yet, so SmartScreen will warn on first run.

    Usage:
        ./installer/build-windows-installer.ps1 -Version 1.0.0
        ./installer/build-windows-installer.ps1 -Version 1.0.0 -ProductName "Amanorsac Digital Bundle" `
            -Targets D01,D02,D03,D04,D05,D06,D07,D08,D09,D10
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$ProductName = "Amanorsac Analog Bundle",
    [string]$Stage = "",
    [string]$BuildDir = "build/win-x64",
    [string[]]$Targets = @("A01","A02","A03","A04","A05","A06","A07","A08","A09","A10"),
    # The product's accent, for the primary button and the progress bar.
    [string]$Accent = "",
    [string]$OneLine = "",
    [switch]$Unlicensed,
    [switch]$AllowUnarmed
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$isDigital = $ProductName -match "Digital"
if (-not $Accent) { $Accent = if ($isDigital) { "#2F8BFF" } else { "#E8B04A" } }
if (-not $OneLine) {
    $OneLine = if ($isDigital) { "Ten precision processors that show you exactly what they are doing to your sound." }
               else { "Ten analog processors: preamps, equalisers, compressors, tape and plate, built from the ground up." }
}

# P28: the product name as the studio writes it, spaces removed.
$fileStem = ($ProductName -replace "\s+", "") + "-$Version-Windows"
$stageDir = if ($Stage) { $Stage } else { Join-Path "installer/stage" $ProductName }

# ---------------------------------------------------------------- 1. stage
if (-not $Stage) {
    Write-Host "Staging payload from $BuildDir"
    if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $stageDir "VST3") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $stageDir "Standalone") | Out-Null

    foreach ($id in $Targets) {
        $artefacts = Join-Path $BuildDir "Amanorsac${id}_artefacts/Release"
        $vst3 = Get-ChildItem (Join-Path $artefacts "VST3") -Filter *.vst3 -Directory -ErrorAction SilentlyContinue
        foreach ($bundle in $vst3) { Copy-Item $bundle.FullName (Join-Path $stageDir "VST3") -Recurse -Force }
        $apps = Get-ChildItem (Join-Path $artefacts "Standalone") -Filter *.exe -ErrorAction SilentlyContinue
        foreach ($app in $apps) { Copy-Item $app.FullName (Join-Path $stageDir "Standalone") -Force }
    }
}

# A customer installer must come from a build with licence enforcement armed.
$armedFile = Join-Path $BuildDir "licensing-armed.txt"
$armed = if (Test-Path $armedFile) { (Get-Content $armedFile -Raw).Trim() } else { "unknown" }
if (-not $Unlicensed -and $armed -ne "1" -and -not $AllowUnarmed) {
    throw "This build has licence enforcement OFF (licensing-armed.txt = $armed). Reconfigure with -DAMANORSAC_LICENSING=ON, or pass -AllowUnarmed for an internal build."
}
if ($AllowUnarmed -and $armed -ne "1") { Write-Warning "Packaging an INTERNAL installer: the plug-ins do not require a licence." }

$bundles = @(Get-ChildItem (Join-Path $stageDir "VST3") -Filter *.vst3 -Directory -ErrorAction SilentlyContinue)
$appCount = @(Get-ChildItem (Join-Path $stageDir "Standalone") -Filter *.exe -ErrorAction SilentlyContinue).Count
Write-Host "Payload: $($bundles.Count) VST3 bundles, $appCount standalone apps"
if ($bundles.Count -lt 1 -or $appCount -lt 1) { throw "Nothing to package. Build the plug-ins first." }
if ($bundles.Count -ne $Targets.Count) {
    throw "Expected $($Targets.Count) VST3 bundles, found $($bundles.Count). Refusing to ship a partial bundle."
}

# ---------------------------------------------------------------- 2. payload
$payload = Join-Path $root "installer/stage/payload.zip"
if (Test-Path $payload) { Remove-Item $payload -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $payload -CompressionLevel Optimal
Write-Host ("Payload archive: {0:N1} MB" -f ((Get-Item $payload).Length / 1MB))

# ---------------------------------------------------------------- 3. README.txt (P26, P27)
$names = ($bundles | ForEach-Object { $_.BaseName }) -join ", "
$activation = if ($Unlicensed) {
@"
NO ACTIVATION
There is no key and no account. It never connects to the internet.
"@ } else {
@"
ACTIVATION
Open any plug-in and enter the licence key from your account at
amanorsac.studio/my-apps. One key unlocks the whole bundle on this computer
and covers two computers.
"@ }

$readme = @"
$($ProductName.ToUpper()) $Version
Amanorsac Studio · amanorsac.studio

WHAT THIS IS
$OneLine
Plug-ins: $names

INSTALLING
Windows   Run $fileStem.exe and follow the installer.

WHAT GETS INSTALLED
  VST3        C:\Program Files\Common Files\VST3\<Plug-in>.vst3
  Standalone  C:\Program Files\Amanorsac Studio\$ProductName\
The installer shows these paths, lets you choose which parts to install,
and lets you add a second plug-in folder if you keep yours somewhere else.

YOUR PRESETS AND SETTINGS
  Windows   %APPDATA%\Amanorsac Studio\Presets\<Plug-in>\
Uninstalling does not delete these.

$activation
IF YOUR DAW DOES NOT SEE IT
1. Rescan plug-ins in your DAW's preferences.
2. Check C:\Program Files\Common Files\VST3 is in your DAW's scan paths.
3. Restart the DAW.
Still missing? hello@amanorsac.studio

UNINSTALLING
Windows   Settings > Apps > $ProductName > Uninstall

LICENCE AND PRIVACY
amanorsac.studio/legal · amanorsac.studio/privacy

SUPPORT
hello@amanorsac.studio
"@
$readmePath = Join-Path $root "installer/stage/README.txt"
# Windows line endings and a BOM so Notepad shows it correctly.
Set-Content -Path $readmePath -Value ($readme -replace "`r?`n", "`r`n") -Encoding utf8

# ------------------------------------------------------- 4. stamp the bootstrapper
$licensed = if ($Unlicensed) { "false" } else { "true" }
$buildInfo = @"
// Generated by installer/build-windows-installer.ps1. Do not edit by hand.
namespace AmanorsacInstaller;

internal static class BuildInfo
{
    public const string ProductName = "$ProductName";
    public const string Version = "$Version";
    public const int PluginCount = $($bundles.Count);
    public const string Accent = "$Accent";
    public const bool Licensed = $licensed;
    public const string OneLine = "$($OneLine -replace '"', '\"')";
}
"@
Set-Content -Path (Join-Path $root "installer/Bootstrapper/BuildInfo.cs") -Value $buildInfo -Encoding utf8

# ---------------------------------------------------------------- 5. build
$icon = Join-Path $root "installer/assets/AmanorsacStudio.ico"
if (-not (Test-Path $icon)) { throw "Missing $icon. Run tools/make_installer_icon.ps1." }
$publish = Join-Path $root "installer/stage/publish"
if (Test-Path $publish) { Remove-Item $publish -Recurse -Force }
dotnet publish installer/Bootstrapper/AmanorsacInstaller.csproj `
    -c Release -r win-x64 --self-contained true `
    -p:PayloadZip="$payload" -p:ReadMe="$readmePath" -p:ProductIcon="$icon" `
    -p:Version=$Version -p:Product="$ProductName" -p:Company="Amanorsac Studio" `
    -o $publish
if ($LASTEXITCODE -ne 0) { throw "Installer build failed" }

# ---------------------------------------------------------------- 6. deliver
New-Item -ItemType Directory -Force -Path (Join-Path $root "dist") | Out-Null
$exe = Join-Path $root "dist/$fileStem.exe"
Copy-Item (Join-Path $publish "AmanorsacInstaller.exe") $exe -Force
(Get-FileHash $exe -Algorithm SHA256).Hash.ToLower() + "  $fileStem.exe" |
    Set-Content -Path "$exe.sha256" -Encoding ascii

Write-Host ""
Write-Host "Installer: $exe"
Write-Host ("Size: {0:N1} MB  ({1} bytes)" -f ((Get-Item $exe).Length / 1MB), (Get-Item $exe).Length)
Write-Host "Signed: NO - SmartScreen will warn until an Authenticode certificate is wired in (standard P31)."
