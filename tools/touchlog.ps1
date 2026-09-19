# touchlog.ps1 - log the raw touch readings of pTOS over the USB console
#
# Sends a break every -Interval ms for -Seconds; each break makes pTOS'
# bring-up monitor print the last raw touch values and the mouse position
# ("[touch]" lines).  The framebuffer dump that follows is ignored.

param(
    [string]$Port = "COM20",
    [int]$Seconds = 90,
    [int]$Interval = 1000
)

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.Open()
$null = $sp.ReadExisting()
$start = Get-Date
$buf = ""
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
    $sp.BreakState = $true
    Start-Sleep -Milliseconds 50
    $sp.BreakState = $false
    Start-Sleep -Milliseconds $Interval
    $buf += $sp.ReadExisting()
    foreach ($m in [regex]::Matches($buf, "\[touch\][^\r\n]*")) {
        "{0,5:N1}s {1}" -f ((Get-Date) - $start).TotalSeconds, $m.Value
    }
    $buf = ""
}
$sp.Close()
