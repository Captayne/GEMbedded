# rescanshared.ps1 - make Windows look at the shared drive again
#
# Run elevated, with the card shared:
#   powershell -ExecutionPolicy Bypass -File D:\Develop\GEMbedded\tools\rescanshared.ps1
#
# The drive is announced for as long as the machine is plugged in, so the
# host enumerates it while the card still belongs to pTOS and is told
# there is no medium.  Whether it ever looks again is the question: this
# forces it to, which separates "the host never asked" from "the host
# asked and did not like the answer".

function Show($when) {
    Write-Host "--- $when ---" -Foreground Cyan
    $d = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Model -like '*pTOS*' }
    if (-not $d) { Write-Host "  no pTOS drive -- is sharing on?" -Foreground Red; return }
    Write-Host ("  {0}  media {1}  partitions '{2}'  status {3}" -f `
        $d.DeviceID, $d.MediaType, $d.Partitions, $d.Status)
    $p = Get-CimInstance Win32_DiskPartition | Where-Object { $_.DiskIndex -eq $d.Index }
    if ($p) { $p | ForEach-Object {
        Write-Host ("  partition {0}: {1}  start sector {2}  {3} sectors" -f `
            $_.Index, $_.Type, ($_.StartingOffset/512), $_.NumberOfBlocks) } }
    else { Write-Host "  no partitions" }
    $m = Get-Disk -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -like '*pTOS*' }
    if ($m) { Write-Host ("  storage service: disk {0}, {1}, {2}" -f $m.Number, $m.PartitionStyle, $m.OperationalStatus) }
    else { Write-Host "  storage service: not listed" }
}

Show "before"
Write-Host "rescanning..." -Foreground Yellow
try { Update-HostStorageCache -ErrorAction Stop } catch { Write-Host ("  Update-HostStorageCache: " + $_.Exception.Message) -Foreground Red }
Start-Sleep -Seconds 3
Show "after rescan"
Write-Host ""
Write-Host "volumes that look like the card (FAT, label PTOS_*):" -Foreground Cyan
Get-CimInstance Win32_LogicalDisk | Where-Object { $_.VolumeName -like 'PTOS*' -or $_.FileSystem -eq 'FAT' } |
    Select-Object DeviceID, DriveType, FileSystem, VolumeName, Size | Format-Table -AutoSize
