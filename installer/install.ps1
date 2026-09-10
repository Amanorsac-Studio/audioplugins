[CmdletBinding()]
param(
    [string]$Vst3Root = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Amanorsac Studio'),
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'Programs\Amanorsac Studio Mixing Suite'),
    [switch]$NoStandalone,
    [switch]$NoShortcuts,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$productName = 'Amanorsac Studio Mixing Suite'
$version = '0.1.2'
$payloadArchive = Join-Path $PSScriptRoot 'payload.zip'

if (-not (Test-Path -LiteralPath $payloadArchive)) {
    throw "Installer payload is missing: $payloadArchive"
}

$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot)
$resolvedVst3Root = [System.IO.Path]::GetFullPath($Vst3Root)
if ([string]::IsNullOrWhiteSpace($resolvedInstallRoot) -or [string]::IsNullOrWhiteSpace($resolvedVst3Root)) {
    throw 'Install destinations must be explicit, non-empty paths.'
}

$unpackRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("AmanorsacInstall_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $unpackRoot -Force | Out-Null

try {
    Expand-Archive -LiteralPath $payloadArchive -DestinationPath $unpackRoot -Force
    $vst3Source = Join-Path $unpackRoot 'VST3'
    $standaloneSource = Join-Path $unpackRoot 'Standalone'
    $bundles = @(Get-ChildItem -LiteralPath $vst3Source -Directory -Filter '*.vst3')
    if ($bundles.Count -ne 21) { throw "Expected 21 VST3 bundles; payload contains $($bundles.Count)." }

    New-Item -ItemType Directory -Path $resolvedVst3Root -Force | Out-Null
    foreach ($bundle in $bundles) {
        $destination = Join-Path $resolvedVst3Root $bundle.Name
        if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
        Copy-Item -LiteralPath $bundle.FullName -Destination $destination -Recurse -Force
    }

    New-Item -ItemType Directory -Path $resolvedInstallRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $unpackRoot 'uninstall.ps1') -Destination (Join-Path $resolvedInstallRoot 'uninstall.ps1') -Force
    Copy-Item -LiteralPath (Join-Path $unpackRoot 'README.md') -Destination (Join-Path $resolvedInstallRoot 'README.md') -Force

    $installedApps = @()
    if (-not $NoStandalone) {
        $appsDirectory = Join-Path $resolvedInstallRoot 'Standalone'
        New-Item -ItemType Directory -Path $appsDirectory -Force | Out-Null
        foreach ($app in Get-ChildItem -LiteralPath $standaloneSource -File -Filter '*.exe') {
            $destination = Join-Path $appsDirectory $app.Name
            Copy-Item -LiteralPath $app.FullName -Destination $destination -Force
            $installedApps += $destination
        }
        if ($installedApps.Count -ne 21) { throw "Expected 21 Standalone apps; installed $($installedApps.Count)." }
    }

    $uninstallCommand = "powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $resolvedInstallRoot 'uninstall.ps1')`" -InstallRoot `"$resolvedInstallRoot`" -Vst3Root `"$resolvedVst3Root`""
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AmanorsacStudioMixingSuite'
    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value $productName -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value $version -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'Amanorsac Studio' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $resolvedInstallRoot -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

    if (-not $NoShortcuts -and $installedApps.Count -gt 0) {
        $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Amanorsac Studio'
        New-Item -ItemType Directory -Path $startMenu -Force | Out-Null
        $shell = New-Object -ComObject WScript.Shell
        foreach ($app in $installedApps) {
            $shortcut = $shell.CreateShortcut((Join-Path $startMenu (([System.IO.Path]::GetFileNameWithoutExtension($app)) + '.lnk')))
            $shortcut.TargetPath = $app
            $shortcut.WorkingDirectory = [System.IO.Path]::GetDirectoryName($app)
            $shortcut.Save()
        }
    }

    if (-not $Quiet) {
        Add-Type -AssemblyName PresentationFramework
        [System.Windows.MessageBox]::Show(
            "Installed 21 VST3 plugins to:`n$resolvedVst3Root`n`nRestart your DAW and rescan VST3 plugins.",
            $productName,
            [System.Windows.MessageBoxButton]::OK,
            [System.Windows.MessageBoxImage]::Information) | Out-Null
    }
    Write-Host "Installed $productName $version"
    Write-Host "VST3: $resolvedVst3Root"
    if (-not $NoStandalone) { Write-Host "Apps: $resolvedInstallRoot" }
}
finally {
    if (Test-Path -LiteralPath $unpackRoot) { Remove-Item -LiteralPath $unpackRoot -Recurse -Force }
}
