# volshared.ps1 - the authoritative view of the shared card's volumes
#
# Run elevated, with the card shared:
#   powershell -ExecutionPolicy Bypass -File D:\Develop\GEMbedded\tools\volshared.ps1
#
# Get-Volume reads MSFT_Volume from the storage service, and the storage
# service demonstrably does not have this disk.  Volumes are made by
# volmgr in the kernel regardless, so they may well exist and simply be
# invisible to that cmdlet.  diskpart and mountvol ask the kernel.
#
# Reads only.  Nothing here writes, formats, initialises or assigns.

$tmp = Join-Path $env:TEMP "volshared.dp.txt"
@"
list disk
list volume
"@ | Set-Content -Encoding ascii $tmp

Write-Host "=== diskpart: disks and volumes ===" -Foreground Cyan
& diskpart /s $tmp
Remove-Item $tmp -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== mountvol: every volume the kernel has ===" -Foreground Cyan
& mountvol

Write-Host ""
Write-Host "=== our disk, and what partmgr made of it ===" -Foreground Cyan
$d = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Model -like '*pTOS*' }
if (-not $d) { Write-Host "  no pTOS drive -- is sharing on?" -Foreground Red; exit 1 }
Write-Host ("  {0}  {1}  index {2}" -f $d.DeviceID, $d.MediaType, $d.Index)
Get-CimInstance Win32_DiskPartition | Where-Object { $_.DiskIndex -eq $d.Index } | ForEach-Object {
    Write-Host ("  partition {0}: {1}  start sector {2}  {3} sectors  bootable {4}" -f `
        $_.Index, $_.Type, ($_.StartingOffset/512), $_.NumberOfBlocks, $_.Bootable)
    # Does a logical disk hang off this partition at all?
    $q = "ASSOCIATORS OF {Win32_DiskPartition.DeviceID='$($_.DeviceID)'} WHERE AssocClass=Win32_LogicalDiskToPartition"
    $ld = Get-CimInstance -Query $q -ErrorAction SilentlyContinue
    if ($ld) { $ld | ForEach-Object { Write-Host ("    -> {0}  {1}  {2}" -f $_.DeviceID, $_.FileSystem, $_.VolumeName) -Foreground Green } }
    else     { Write-Host "    -> no logical disk on this partition" -Foreground Yellow }
}
