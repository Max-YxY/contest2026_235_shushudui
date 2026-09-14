param(
  [Parameter(Mandatory = $true)] [string] $PackagePath,
  [string] $VmHost = 'max@192.168.182.129',
  [string] $IdentityFile = (Join-Path $PSScriptRoot '..\.codex_vm_ssh_ed25519')
)

$ErrorActionPreference = 'Stop'
$slotOffset = 0xEB3000
$slotBytes = 0x14D000
$package = Get-Item -LiteralPath $PackagePath
if ($package.Length -ne 1228864) { throw "Expected one 1024x600 RGB565 package (1228864 bytes), got $($package.Length)." }
if ($package.Length -gt $slotBytes) { throw 'Package exceeds verified image slot.' }
$bytes = [IO.File]::ReadAllBytes($package.FullName)
if ([Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne 'OVIP4IMG') { throw 'Package magic is invalid.' }
if ([BitConverter]::ToUInt32($bytes, 8) -ne 1 -or [BitConverter]::ToUInt32($bytes, 12) -ne 64) { throw 'Package version or header size is invalid.' }
if ([BitConverter]::ToUInt32($bytes, 16) -ne 1024 -or [BitConverter]::ToUInt32($bytes, 20) -ne 600 -or [BitConverter]::ToUInt32($bytes, 24) -ne 1 -or [BitConverter]::ToUInt32($bytes, 28) -ne 1228800) { throw 'Package image geometry or pixel format is invalid.' }
$actualPayloadHash = [Security.Cryptography.SHA256]::Create().ComputeHash($bytes[64..($bytes.Length - 1)])
for ($i = 0; $i -lt 32; $i++) { if ($actualPayloadHash[$i] -ne $bytes[32 + $i]) { throw 'Package payload SHA-256 does not match its header.' } }

$remote = '/tmp/openvela-upload-image.ovip'
scp -i $IdentityFile -o BatchMode=yes $package.FullName "${VmHost}:$remote"
ssh -i $IdentityFile -o BatchMode=yes $VmHost "/home/max/.local/bin/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 write-flash 0xEB3000 $remote && /home/max/.local/bin/esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 verify-flash 0xEB3000 $remote"
