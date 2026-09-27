# forgetshared.ps1 - make Windows forget the drives we used to be
#
# Run elevated, with sharing OFF:
#   powershell -ExecutionPolicy Bypass -File D:\Develop\GEMbedded\tools\forgetshared.ps1
#
# Over one evening this machine offered the host several different
# devices: a flash drive, then an SD card, first with a serial number
# Windows rejected (so its identity came from the hub port) and then with
# a valid one.  Each left an entry behind, and one of them was a volume
# spanning the whole disk that held the letter G:.  Windows decides things
# about a device once and keeps them, so testing against those entries is
# testing against our own earlier mistakes.
#
# This removes only entries whose instance id says VEN_PTOS and which are
# NOT currently present.  A device that is plugged in is left alone.

$all = Get-PnpDevice -ErrorAction SilentlyContinue |
       Where-Object { $_.InstanceId -match 'VEN_PTOS|PROD_FLASH_DRIVE|PROD_SD_CARD' }

$present = $all | Where-Object { $_.Status -eq 'OK' }
$ghosts  = $all | Where-Object { $_.Status -ne 'OK' }

Write-Host "=== present (left alone) ===" -Foreground Cyan
if ($present) { $present | ForEach-Object { Write-Host ("  {0}  {1}" -f $_.Class, $_.InstanceId) } }
else { Write-Host "  none -- good, sharing is off" }

Write-Host "=== leftovers to remove ===" -Foreground Cyan
if (-not $ghosts) { Write-Host "  none"; exit 0 }
$ghosts | ForEach-Object { Write-Host ("  {0}  {1}  [{2}]" -f $_.Class, $_.InstanceId, $_.FriendlyName) }

Write-Host ""
foreach ($g in $ghosts) {
    Write-Host ("removing {0}" -f $g.InstanceId) -Foreground Yellow
    & pnputil /remove-device "$($g.InstanceId)" 2>&1 | ForEach-Object { "  $_" }
}

Write-Host ""
Write-Host "What this does NOT touch: the mount manager's record of which" -Foreground Cyan
Write-Host "letter each volume had (HKLM\SYSTEM\MountedDevices).  Those are" -Foreground Cyan
Write-Host "keyed by the card's own MBR signature, so they belong to the" -Foreground Cyan
Write-Host "card and not to us, and the card reader uses them too." -Foreground Cyan
