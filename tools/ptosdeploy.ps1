# ptosdeploy.ps1 - send a program to a pTOS3000 machine over the USB console
#
# The same transfer as tools/ptosdeploy.py, without needing pyserial.
# DEPLOY.PRG must be running on the machine.
#
#   powershell -File ptosdeploy.ps1 [-Port COM20] [-Name NAME.PRG] [-NoRun] FILE

param(
    [Parameter(Mandatory = $true, Position = 0)] [string]$File,
    [string]$Port = "COM20",
    [string]$Name,
    [switch]$NoRun,
    [double]$Timeout = 10.0
)

$data = [System.IO.File]::ReadAllBytes((Resolve-Path $File))
if (-not $Name) { $Name = [System.IO.Path]::GetFileName($File) }
$Name = $Name.ToUpper()
if ($Name.Length -gt 12) { throw "the name must be a plain 8.3 file name" }

# CRC32 of the data (PowerShell 5.1 needs every step masked back to 32 bits)
$table = New-Object uint32[] 256
for ($i = 0; $i -lt 256; $i++) {
    $c = [uint32]$i
    for ($k = 0; $k -lt 8; $k++) {
        if ($c -band 1) {
            $c = [uint32]((0xEDB88320 -bxor ($c -shr 1)) -band 0xFFFFFFFF)
        } else {
            $c = [uint32]($c -shr 1)
        }
    }
    $table[$i] = $c
}
$crc = [uint32]::MaxValue
foreach ($b in $data) {
    $idx = [int](($crc -bxor $b) -band 0xFF)
    $crc = [uint32](($table[$idx] -bxor ($crc -shr 8)) -band 0xFFFFFFFF)
}
$crc = [uint32](($crc -bxor [uint32]::MaxValue) -band 0xFFFFFFFF)

$nameBytes = [System.Text.Encoding]::ASCII.GetBytes($Name)
$header = New-Object System.Collections.Generic.List[byte]
$header.AddRange([System.Text.Encoding]::ASCII.GetBytes("PTUP1"))
$header.AddRange([BitConverter]::GetBytes([uint16]$(if ($NoRun) { 0 } else { 1 })))
$header.AddRange([BitConverter]::GetBytes([uint16]$nameBytes.Length))
$header.AddRange($nameBytes)
$header.AddRange([BitConverter]::GetBytes([uint32]$data.Length))
$header.AddRange([BitConverter]::GetBytes($crc))

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.ReadTimeout = 200
$sp.WriteTimeout = 5000
$sp.DtrEnable = $true
$sp.Open()
try {
    $sp.DiscardInBuffer()
    $sp.Write($header.ToArray(), 0, $header.Count)
    $sent = 0
    while ($sent -lt $data.Length) {
        $n = [Math]::Min(4096, $data.Length - $sent)
        $sp.Write($data, $sent, $n)
        $sent += $n
        Write-Host -NoNewline ("`r{0,7} / {1} bytes" -f $sent, $data.Length)
    }
    Write-Host ""

    $deadline = (Get-Date).AddSeconds($Timeout)
    $line = ""
    while ((Get-Date) -lt $deadline) {
        try { $ch = [char]$sp.ReadByte() } catch { continue }
        if ($ch -eq "`r" -or $ch -eq "`n") { if ($line) { break } else { continue } }
        $line += $ch
    }
} finally {
    $sp.Close()
}

if (-not $line) { Write-Error "no answer -- is DEPLOY.PRG running?"; exit 1 }
Write-Host $line
if ($line.StartsWith("+")) { exit 0 } else { exit 1 }
