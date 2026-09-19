[CmdletBinding()]
param(
    [string]$Vst3Root = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Amanorsac Studio'),
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [string]$ProductName = 'Amanorsac product',
    [switch]$NoShortcuts,
    [switch]$NoRegistry,
    [switch]$Quiet
)

# Removes ONE Amanorsac product. Several products share the VST3 folder and the
# Start Menu folder, so this removes only the names recorded in installed.txt
# by that product's installer, and deletes a shared folder only once it is empty.

$ErrorActionPreference = 'Stop'
$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$resolvedVst3Root = [System.IO.Path]::GetFullPath($Vst3Root)
$manifestPath = Join-Path $resolvedInstallRoot 'installed.txt'

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Cannot find $manifestPath, so there is no record of what this product installed. Nothing was removed."
}

$entries = Get-Content -LiteralPath $manifestPath | Where-Object { $_ -match '\|' }
$recorded = ($entries | Where-Object { $_ -like 'product|*' } | Select-Object -First 1)
if ($recorded) { $ProductName = $recorded.Substring('product|'.Length) }

if (-not $Quiet) {
    Add-Type -AssemblyName PresentationFramework
    $answer = [System.Windows.MessageBox]::Show(
        "Remove $ProductName plug-ins and Standalone apps?`n`nOther Amanorsac products are not affected.",
        "Uninstall $ProductName",
        [System.Windows.MessageBoxButton]::YesNo,
        [System.Windows.MessageBoxImage]::Question)
    if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { exit 0 }
}

$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Amanorsac Studio'

foreach ($entry in $entries) {
    $kind, $name = $entry.Split('|', 2)
    if ($kind -eq 'vst3') {
        $bundle = Join-Path $resolvedVst3Root $name
        if (Test-Path -LiteralPath $bundle) { Remove-Item -LiteralPath $bundle -Recurse -Force }
    }
    elseif ($kind -eq 'app' -and -not $NoShortcuts) {
        $shortcut = Join-Path $startMenu ($name + '.lnk')
        if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
    }
}

# A shared folder goes only when nothing of any product is left in it.
foreach ($shared in @($resolvedVst3Root, $startMenu)) {
    if ((Test-Path -LiteralPath $shared) -and -not (Get-ChildItem -LiteralPath $shared -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $shared -Force
    }
}

if (-not $NoRegistry) {
    $keyName = 'Amanorsac_' + (($ProductName.ToCharArray() | Where-Object { [char]::IsLetterOrDigit($_) }) -join '')
    $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$keyName"
    if (Test-Path $uninstallKey) { Remove-Item -Path $uninstallKey -Recurse -Force }
}

Write-Host "$ProductName was removed."
if (Test-Path -LiteralPath $resolvedInstallRoot) {
    Remove-Item -LiteralPath $resolvedInstallRoot -Recurse -Force
}
