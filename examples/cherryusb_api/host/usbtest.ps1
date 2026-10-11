# Windows PowerShell: wait for the YuzukiNeko composite device (VID 1209, PID 0002), then
#  1. read README.TXT from its mass storage volume and write a file to it
#  2. send text to its CDC ACM COM port, check that it comes back, and send a break
$end = (Get-Date).AddSeconds(40)
$dev = $null
while ((Get-Date) -lt $end -and -not $dev) {
    Start-Sleep 1
    $dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like '*VID_1209&PID_0002*' -and $_.Name -match 'COM\d+' } | Select-Object -First 1
}
if (-not $dev) { Write-Output "device not found"; exit 1 }
Write-Output ("serial: " + $dev.Name)

$vol = $null
while ((Get-Date) -lt $end -and -not $vol) {
    Start-Sleep 1
    $vol = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'F101DISK' -and $_.DriveLetter } | Select-Object -First 1
}
if ($vol) {
    $dl = $vol.DriveLetter + ":"
    Write-Output ("volume: " + $dl + " " + $vol.FileSystemLabel + " " + $vol.FileSystem + " " + $vol.Size + " bytes")
    Write-Output ("README.TXT: " + (Get-Content "$dl\README.TXT" -Raw).Trim())
    Set-Content -Path "$dl\HELLO.TXT" -Value "written by the host" -Encoding ASCII
    Write-Output ("HELLO.TXT: " + (Get-Content "$dl\HELLO.TXT" -Raw).Trim())
    Start-Sleep 1
} else {
    Write-Output "volume not found"
}

$com = [regex]::Match($dev.Name, 'COM\d+').Value
$p = New-Object System.IO.Ports.SerialPort $com, 115200, 'None', 8, 'One'
$p.DtrEnable = $true
$p.RtsEnable = $true
$p.ReadTimeout = 3000
$p.Open()
Start-Sleep -Milliseconds 500
$p.Write("hello from the host")
Start-Sleep -Milliseconds 800
Write-Output ("echo 1: " + $p.ReadExisting())
$big = "0123456789" * 100
$p.Write($big)
Start-Sleep -Milliseconds 1500
$back = $p.ReadExisting()
Write-Output ("echo 2: sent " + $big.Length + " bytes, got " + $back.Length + " bytes, same = " + ($back -eq $big))
$p.BreakState = $true
Start-Sleep -Milliseconds 200
$p.BreakState = $false
$p.Close()

# let Windows suspend the idle device and accept its remote wakeup (needs administrator rights)
foreach ($cls in 'MSPower_DeviceEnable', 'MSPower_DeviceWakeEnable') {
    try {
        $n = 0
        Get-CimInstance -Namespace root\wmi -ClassName $cls -ErrorAction Stop | Where-Object { $_.InstanceName -like '*VID_1209&PID_0002*' } | ForEach-Object {
            $_.Enable = $true
            Set-CimInstance -InputObject $_ -ErrorAction Stop
            $n++
        }
        Write-Output ("${cls}: enabled on " + $n + " instance(s)")
    } catch {
        Write-Output ("${cls}: " + $_.Exception.Message)
    }
}
Start-Sleep 30
