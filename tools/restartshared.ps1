# restartshared.ps1 - make Windows enumerate the shared drive afresh
#
# Run elevated, with the card shared:
#   powershell -ExecutionPolicy Bypass -File D:\Develop\GEMbedded\tools\restartshared.ps1
#
# The drive is announced for as long as the machine is plugged in, because
# it shares its USB device with the serial console.  So Windows enumerates
# it while the card still belongs to pTOS, is told there is no medium, and
# keeps that.  Restarting just the disk device -- not the whole composite
# device, so the console keeps its COM port -- makes Windows build the
# stack again, and this time the medium is there.
#
# Nothing here writes to the card or initialises anything.

$dev = Get-PnpDevice -ErrorAction SilentlyContinue |
       Where-Object { $_.InstanceId -like 'USBSTOR\DISK&VEN_PTOS*' }
if (-not $dev) { Write-Host "No pTOS disk device -- is sharing on?" -Foreground Red; exit 1 }
Write-Host ("device : {0}" -f $dev.InstanceId)
Write-Host ("status : {0} / {1}" -f $dev.Status, $dev.Problem)

function Report($when) {
    Write-Host "--- $when ---" -Foreground Cyan
    $d = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Model -like '*pTOS*' }
    if ($d) {
        Write-Host ("  {0}  {1}" -f $d.DeviceID, $d.MediaType)
        $p = Get-CimInstance Win32_DiskPartition | Where-Object { $_.DiskIndex -eq $d.Index }
        if ($p) { $p | ForEach-Object { Write-Host ("  partition {0} at sector {1}" -f $_.Index, ($_.StartingOffset/512)) } }
        else { Write-Host "  no partitions" }
    } else { Write-Host "  drive gone" }
    $v = Get-Volume -ErrorAction SilentlyContinue |
         Where-Object { $_.FileSystemType -match 'FAT' -or $_.FileSystemLabel -like 'PTOS*' }
    if ($v) { $v | ForEach-Object {
        Write-Host ("  VOLUME {0}: {1} {2} {3} MB" -f $_.DriveLetter, $_.FileSystemLabel, $_.FileSystemType, [math]::Round($_.Size/1MB)) -Foreground Green } }
    else { Write-Host "  no FAT volume" -Foreground Yellow }
}

Report "before"

Write-Host "restarting the disk device..." -Foreground Yellow
& pnputil /restart-device "$($dev.InstanceId)" 2>&1 | ForEach-Object { "  $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Host "  pnputil did not take it; disabling and re-enabling instead" -Foreground Yellow
    Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Enable-PnpDevice  -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 5
Report "after restarting the device"

$v = Get-Volume -ErrorAction SilentlyContinue | Where-Object { $_.FileSystemType -match 'FAT' }
if (-not $v) {
    Write-Host ""
    Write-Host "still nothing; the storage service has a stale view of this disk" -Foreground Yellow
    Write-Host "(it is missing from MSFT_Disk while partmgr has it), so restart it too" -Foreground Yellow
    Restart-Service StorSvc -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 4
    Report "after restarting StorSvc"
}
