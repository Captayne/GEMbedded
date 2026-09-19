# watch.ps1 - watch pTOS over the USB console: stack usage and faults
#
# Sends a break every -Interval seconds; pTOS' bring-up monitor answers
# with the pc, the free space left on its stacks ("[stack free]") and the
# touch state.  Everything else the console prints is kept too, so that a
# fault report is not lost.  Stops at the first fault report.

param(
    [string]$Port = "COM20",
    [int]$Seconds = 300,
    [int]$Interval = 5,
    [string]$Log = "watch.log"
)

$n = 0
while (-not ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) -and $n -lt 60) {
    Start-Sleep -Milliseconds 500; $n++
}
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.Open()
$start = Get-Date
$all = ""
$next = $start
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
    if ((Get-Date) -ge $next) {
        $sp.BreakState = $true
        Start-Sleep -Milliseconds 50
        $sp.BreakState = $false
        $next = (Get-Date).AddSeconds($Interval)
    }
    Start-Sleep -Milliseconds 200
    try { $all += $sp.ReadExisting() } catch { $all += "`n[port lost]`n"; break }
    if ($all -match "\*\*\* fault") {
        Start-Sleep -Seconds 2
        try { $all += $sp.ReadExisting() } catch {}
        break
    }
}
$sp.Close()
$all | Out-File -Encoding ascii $Log
$lines = $all -split "`r?`n" | Where-Object { $_ -notmatch "^\[fb\]" -and $_.Trim() -ne "" }
$lines | Select-String -Pattern "stack free|\*\*\*|pc=|fsr=|r[0-9]+=|touch" | Select-Object -Last 40 | ForEach-Object { $_.Line }
