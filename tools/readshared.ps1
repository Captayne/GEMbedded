# readshared.ps1 - read sector 0 of the shared pTOS drive, as Windows sees it
#
# Raw access to a physical drive needs administrator rights, so run this
# from an elevated PowerShell while the machine has the card shared:
#
#     powershell -ExecutionPolicy Bypass -File D:\Develop\GEMbedded\tools\readshared.ps1
#
# The point is the comparison: the card's own MBR is known good (Windows
# mounts it through a card reader), and the driver on the machine reports
# sending it complete and acknowledged.  So the one thing nobody has seen
# is what arrives here.

$drive = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Model -like '*pTOS*' }
if (-not $drive) { Write-Host "No pTOS drive found -- is sharing on?" -Foreground Red; exit 1 }
Write-Host ("device : {0}  ({1})" -f $drive.DeviceID, $drive.Model)

try {
    $fs = New-Object System.IO.FileStream($drive.DeviceID,
              [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
              [System.IO.FileShare]::ReadWrite)
} catch {
    Write-Host ("cannot open: " + $_.Exception.Message) -Foreground Red
    Write-Host "This needs an elevated PowerShell." -Foreground Yellow
    exit 1
}

$b = New-Object byte[] 512
$n = 0
try { $n = $fs.Read($b, 0, 512) } catch { Write-Host ("read failed: " + $_.Exception.Message) -Foreground Red }
$fs.Close()
Write-Host ("read   : {0} bytes" -f $n)
if ($n -lt 512) { exit 1 }

Write-Host ("boot sig : {0:X2}{1:X2}    (expect AA55)" -f $b[0x1ff], $b[0x1fe])
Write-Host ("disk sig : 0x{0:X2}{1:X2}{2:X2}{3:X2}  (expect 0x8B52E3D9)" -f $b[0x1bb],$b[0x1ba],$b[0x1b9],$b[0x1b8])
foreach ($i in 0..3) {
    $o = 0x1be + $i * 16
    if ($b[$o+4] -ne 0) {
        Write-Host ("entry {0}  : type {1:X2} start {2} count {3}" -f `
            $i, $b[$o+4], [BitConverter]::ToUInt32($b,$o+8), [BitConverter]::ToUInt32($b,$o+12))
    }
}
Write-Host "expect   : entry 0 type 0E start 128 count 3891200"
Write-Host "           entry 1 type 0E start 3891328 count 3853184"
Write-Host ""
Write-Host "first 32 bytes of the sector:"
Write-Host ("  " + (($b[0..31] | ForEach-Object { $_.ToString('x2') }) -join ' '))
Write-Host "last 32 bytes of the sector:"
Write-Host ("  " + (($b[480..511] | ForEach-Object { $_.ToString('x2') }) -join ' '))
