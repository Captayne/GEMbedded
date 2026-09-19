# fbshot.ps1 - screenshot of the pTOS framebuffer over the USB console
#
# Sends a break to the console; pTOS' bring-up monitor answers with the
# interrupted pc and, with the SPI display configured, the 320x240 1 bpp
# framebuffer (native little-endian words, bit 15 = leftmost pixel)
# as "[fb]" hex lines.  Those are turned into a PNG.
#
#   powershell -File fbshot.ps1 [-Port COM20] [-Out shot.png] [-Scale 2]

param(
    [string]$Port = "COM20",
    [string]$Out = "fbshot.png",
    [int]$Scale = 2
)

Add-Type -AssemblyName System.Drawing

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.Open()
$null = $sp.ReadExisting()
$sp.BreakState = $true
Start-Sleep -Milliseconds 100
$sp.BreakState = $false

$text = ""
$deadline = (Get-Date).AddSeconds(10)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 200
    $text += $sp.ReadExisting()
    if (([regex]::Matches($text, "\[fb\][0-9a-f]{80}")).Count -ge 240) { break }
}
$sp.Close()

$mon = [regex]::Match($text, "\[mon\][^\r\n]*")
if ($mon.Success) { Write-Host $mon.Value }

$rows = [regex]::Matches($text, "\[fb\]([0-9a-f]{80})")
if ($rows.Count -lt 240) {
    Write-Host "only $($rows.Count) of 240 lines received"
    if ($rows.Count -eq 0) { exit 1 }
}

$w = 320; $h = 240
$bmp = New-Object System.Drawing.Bitmap ($w * $Scale), ($h * $Scale)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::White)
$black = [System.Drawing.Brushes]::Black
for ($y = 0; $y -lt $rows.Count -and $y -lt $h; $y++) {
    $hex = $rows[$y].Groups[1].Value
    for ($b = 0; $b -lt 40; $b++) {
        # native little-endian words, bit 15 leftmost: pixels 0-7 are in
        # the second byte of each word
        $v = [Convert]::ToInt32($hex.Substring(($b -bxor 1) * 2, 2), 16)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if ($v -band (0x80 -shr $bit)) {
                $g.FillRectangle($black, ($b * 8 + $bit) * $Scale, $y * $Scale, $Scale, $Scale)
            }
        }
    }
}
$g.Dispose()
$path = [System.IO.Path]::GetFullPath($Out)
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $path"
