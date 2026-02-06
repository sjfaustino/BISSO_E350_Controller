$port = New-Object System.IO.Ports.SerialPort COM6, 115200, None, 8, One
$port.ReadTimeout = 5000
$port.Open()
# Trigger a reboot via DTR/RTS if possible, or just wait for existing output
$port.DtrEnable = $false
Start-Sleep -Milliseconds 100
$port.DtrEnable = $true
Start-Sleep -Seconds 10
$data = $port.ReadExisting()
$port.Close()
$data
