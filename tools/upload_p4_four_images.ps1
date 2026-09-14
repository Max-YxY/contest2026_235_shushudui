param(
  [Parameter(Mandatory = $true)] [string] $Normal,
  [Parameter(Mandatory = $true)] [string] $Stain,
  [Parameter(Mandatory = $true)] [string] $Damage,
  [Parameter(Mandatory = $true)] [string] $Wrinkle,
  [string] $OutputDirectory = (Join-Path $PSScriptRoot '..\artifacts\p4-user-upload'),
  [string] $VmHost = 'max@192.168.182.129',
  [string] $IdentityFile = (Join-Path $PSScriptRoot '..\.codex_vm_ssh_ed25519')
)

$ErrorActionPreference = 'Stop'

$sources = @{
  NOR = (Resolve-Path -LiteralPath $Normal).Path
  STA = (Resolve-Path -LiteralPath $Stain).Path
  DAM = (Resolve-Path -LiteralPath $Damage).Path
  WRI = (Resolve-Path -LiteralPath $Wrinkle).Path
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$manifestPath = Join-Path $OutputDirectory 'four-images-manifest.json'
$packagePath = Join-Path $OutputDirectory 'four-images.ovip'
$manifest = [ordered]@{
  items = @(
    [ordered]@{ expected_class = 'NOR'; source_image = $sources.NOR },
    [ordered]@{ expected_class = 'STA'; source_image = $sources.STA },
    [ordered]@{ expected_class = 'DAM'; source_image = $sources.DAM },
    [ordered]@{ expected_class = 'WRI'; source_image = $sources.WRI }
  )
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$python = Get-Command python -ErrorAction Stop
& $python.Source (Join-Path $PSScriptRoot 'build_p4_demo_carousel.py') `
  --manifest $manifestPath --output $packagePath
if ($LASTEXITCODE -ne 0) { throw 'Carousel package build failed.' }

$uploader = Join-Path $PSScriptRoot 'upload_p4_demo_carousel.ps1'
& $uploader `
  -PackagePath $packagePath -VmHost $VmHost -IdentityFile $IdentityFile
if ($LASTEXITCODE -ne 0) { throw 'Image upload or flash verification failed.' }

$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
Write-Host "Upload complete: $packagePath"
Write-Host "SHA-256: $hash"
Write-Host 'Verified image-slot-only write at 0xEB3000; application and model areas were not written.'
