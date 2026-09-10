[CmdletBinding()]
param(
    [string]$Vst3Root = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Amanorsac Studio'),
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'Programs\Amanorsac Studio Mixing Suite'),
    [switch]$NoShortcuts,
    [switch]$NoRegistry,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$resolvedVst3Root = [System.IO.Path]::GetFullPath($Vst3Root)

if (-not $Quiet) {
    Add-Type -AssemblyName PresentationFramework
    $answer = [System.Windows.MessageBox]::Show(
        'Remove Amanorsac Studio Mixing Suite plugins and Standalone apps?',
        'Uninstall Amanorsac Studio Mixing Suite',
        [System.Windows.MessageBoxButton]::YesNo,
        [System.Windows.MessageBoxImage]::Question)
    if ($answer -ne [System.Windows.MessageBoxResult]::Yes) { exit 0 }
}

$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Amanorsac Studio'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AmanorsacStudioMixingSuite'
if (Test-Path -LiteralPath $resolvedVst3Root) { Remove-Item -LiteralPath $resolvedVst3Root -Recurse -Force }
if (-not $NoShortcuts -and (Test-Path -LiteralPath $startMenu)) { Remove-Item -LiteralPath $startMenu -Recurse -Force }
if (-not $NoRegistry -and (Test-Path $uninstallKey)) { Remove-Item -Path $uninstallKey -Recurse -Force }

Write-Host 'Amanorsac Studio Mixing Suite was removed.'
if (Test-Path -LiteralPath $resolvedInstallRoot) {
    Remove-Item -LiteralPath $resolvedInstallRoot -Recurse -Force
}
