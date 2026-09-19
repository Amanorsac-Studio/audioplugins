[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [switch]$NoRegistry,
    [switch]$Quiet
)

# Removes ONE Amanorsac product: exactly the files its installer recorded in
# installed.txt, and nothing else. Other Amanorsac products are untouched, and
# so are the buyer's own presets and settings, which never live in these folders.

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath($InstallRoot)
$manifestPath = Join-Path $root 'installed.txt'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Cannot find $manifestPath, so there is no record of what was installed. Nothing was removed."
}

$entries = Get-Content -LiteralPath $manifestPath | Where-Object { $_ -match '\|' }
$productLine = $entries | Where-Object { $_ -like 'product|*' } | Select-Object -First 1
$product = if ($productLine) { $productLine.Substring('product|'.Length) } else { 'this Amanorsac product' }

if (-not $Quiet) {
    Add-Type -AssemblyName PresentationFramework
    $answer = [System.Windows.MessageBox]::Show(
        "Remove $product?`n`nYour own presets and settings are kept. Other Amanorsac products are not affected.",
        "Uninstall $product",
        [System.Windows.MessageBoxButton]::YesNo,
        [System.Windows.MessageBoxImage]::Question)
    if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { exit 0 }
}

# The files are in folders Windows protects, so removing them needs the same
# one permission the installer asked for.
$needsAdmin = $entries | Where-Object { $_ -like 'path|*' } | ForEach-Object { $_.Substring(5) } |
    Where-Object { $_ -like "$env:ProgramFiles*" -or $_ -like "$env:ProgramData*" } | Select-Object -First 1
$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if ($needsAdmin -and -not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
                   '-InstallRoot', "`"$root`"", '-Quiet')
    if ($NoRegistry) { $arguments += '-NoRegistry' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait
    exit 0
}

foreach ($entry in $entries) {
    $kind, $value = $entry.Split('|', 2)
    if ($kind -ne 'path') { continue }
    if (Test-Path -LiteralPath $value) { Remove-Item -LiteralPath $value -Recurse -Force }
}

# The shared Start Menu folder goes only when no Amanorsac product is left in it.
$sharedMenu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Amanorsac Studio'
if ((Test-Path -LiteralPath $sharedMenu) -and -not (Get-ChildItem -LiteralPath $sharedMenu -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $sharedMenu -Force
}

if (-not $NoRegistry) {
    $keyName = 'Amanorsac_' + (($product.ToCharArray() | Where-Object { [char]::IsLetterOrDigit($_) }) -join '')
    $uninstallKey = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$keyName"
    if (Test-Path $uninstallKey) { Remove-Item -Path $uninstallKey -Recurse -Force }
}

# Last: the product folder itself, which holds this script.
Set-Location $env:TEMP
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
$studioFolder = Split-Path -Parent $root
if ((Test-Path -LiteralPath $studioFolder) -and (Split-Path -Leaf $studioFolder) -eq 'Amanorsac Studio' -and
    -not (Get-ChildItem -LiteralPath $studioFolder -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $studioFolder -Force
}
Write-Host "$product was removed."
