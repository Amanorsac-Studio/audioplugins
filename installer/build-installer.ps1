[CmdletBinding()]
param(
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $repoRoot 'build\windows-vs2026'
$stageRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'AmanorsacInstallerStage'
$payloadRoot = Join-Path $stageRoot 'payload'
$distRoot = Join-Path $repoRoot 'dist'
$installerName = 'Amanorsac_Studio_Mixing_Suite_0.1.2_Windows.exe'
$targetPath = Join-Path $distRoot $installerName
$packageTarget = Join-Path $stageRoot $installerName
if (-not (Test-Path -LiteralPath $buildRoot)) { throw "Build directory was not found: $buildRoot" }

if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'VST3') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'Standalone') -Force | Out-Null
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null

$bundles = @(Get-ChildItem -LiteralPath $buildRoot -Recurse -Directory -Filter '*.vst3' |
    Where-Object { $_.FullName -match ("\\" + [regex]::Escape($Configuration) + "\\VST3\\") })
$apps = @(Get-ChildItem -LiteralPath $buildRoot -Recurse -File -Filter '*.exe' |
    Where-Object { $_.FullName -match ("\\" + [regex]::Escape($Configuration) + "\\Standalone\\") })
if ($bundles.Count -ne 21) { throw "Expected 21 $Configuration VST3 bundles; found $($bundles.Count)." }
if ($apps.Count -ne 21) { throw "Expected 21 $Configuration Standalone apps; found $($apps.Count)." }

foreach ($bundle in $bundles) { Copy-Item -LiteralPath $bundle.FullName -Destination (Join-Path $payloadRoot 'VST3') -Recurse -Force }
foreach ($app in $apps) { Copy-Item -LiteralPath $app.FullName -Destination (Join-Path $payloadRoot 'Standalone') -Force }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall.ps1') -Destination $payloadRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $payloadRoot -Force

$payloadZip = Join-Path $stageRoot 'payload.zip'
Compress-Archive -Path (Join-Path $payloadRoot '*') -DestinationPath $payloadZip -CompressionLevel Optimal
if (Test-Path -LiteralPath $targetPath) { Remove-Item -LiteralPath $targetPath -Force }
$publishRoot = Join-Path $stageRoot 'publish'
$projectPath = Join-Path $PSScriptRoot 'Bootstrapper\AmanorsacInstaller.csproj'
& dotnet publish $projectPath -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true "-p:PayloadZip=$payloadZip" -o $publishRoot
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed with exit code $LASTEXITCODE." }
$publishedInstaller = Join-Path $publishRoot $installerName
if (-not (Test-Path -LiteralPath $publishedInstaller)) { throw "Published installer was not found: $publishedInstaller" }
Copy-Item -LiteralPath $publishedInstaller -Destination $targetPath -Force
$hash = Get-FileHash -LiteralPath $targetPath -Algorithm SHA256
$hashLine = "$($hash.Hash.ToLowerInvariant())  $installerName`r`n"
[System.IO.File]::WriteAllText((Join-Path $distRoot ($installerName + '.sha256')), $hashLine, [System.Text.Encoding]::ASCII)
Write-Host "Created: $targetPath"
Write-Host "Size: $((Get-Item -LiteralPath $targetPath).Length) bytes"
Write-Host "SHA256: $($hash.Hash.ToLowerInvariant())"
