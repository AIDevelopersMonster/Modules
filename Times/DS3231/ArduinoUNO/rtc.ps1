# rtc.ps1 - Windows PowerShell / PowerShell 7 client for DS3231 UNO sketch.
# No firmware changes required if DS3231_UNO_Time_Serial.ino is already uploaded.
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Ports', 'Sync', 'Read', 'Monitor')]
    [string]$Action = 'Ports',

    [Parameter(Position = 1)]
    [string]$Port,

    [ValidateRange(2, 30)]
    [int]$TimeoutSeconds = 7,

    [switch]$Utc
)

$ErrorActionPreference = 'Stop'

# Enumerate COM ports without opening them.
if ($Action -eq 'Ports') {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($ports.Count -eq 0) {
        Write-Warning 'No serial ports found.'
    } else {
        Write-Host 'Available serial ports:'
        $ports | ForEach-Object { Write-Host "  $_" }
    }
    return
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    throw 'Specify the Arduino port: .\rtc.ps1 Sync COM4 (replace COM4 with your port).'
}

function Read-SerialLine {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [datetime]$DeadlineUtc
    )
    while ([datetime]::UtcNow -lt $DeadlineUtc) {
        try {
            $message = $Serial.ReadLine().Trim()
            if ($message.Length -gt 0) {
                return $message
            }
        }
        catch [System.TimeoutException] {
            # Normal when the UNO has not sent a complete line yet.
        }
    }
    return $null
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    9600,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.NewLine = "`n"
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.ReadTimeout = 350
$serial.WriteTimeout = 2000

try {
    $serial.Open()

    # USB serial opens can reset an Arduino UNO via DTR; wait for bootloader.
    Start-Sleep -Milliseconds 2500
    $serial.DiscardInBuffer()

    if ($Action -eq 'Sync') {
        # Sample PC time only AFTER Arduino has completed its reset.
        # The DS3231 keeps naive wall time, not a timezone identifier.
        $pcTime = if ($Utc) { [datetime]::UtcNow } else { [datetime]::Now }
        $stamp = $pcTime.ToString('yyyy-MM-dd HH:mm:ss',
            [System.Globalization.CultureInfo]::InvariantCulture)
        $basis = if ($Utc) { 'UTC' } else { 'Windows local time' }
        Write-Host "Sending $basis to $Port : $stamp"
        $serial.WriteLine("SET $stamp")

        $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        $confirmed = $false
        while ([datetime]::UtcNow -lt $deadline) {
            $message = Read-SerialLine -Serial $serial -DeadlineUtc $deadline
            if ($null -eq $message) { break }
            if ($message -match '^ERROR:') { throw $message }
            if ($message -eq 'OK: RTC time set') {
                $confirmed = $true
                Write-Host 'RTC accepted the new date/time.' -ForegroundColor Green
                break
            }
        }
        if (-not $confirmed) {
            throw "No SET confirmation from Arduino on $Port. Check the port, firmware and wiring."
        }

        # The firmware prints the RTC reading immediately after confirmation.
        $readback = Read-SerialLine -Serial $serial -DeadlineUtc ([datetime]::UtcNow.AddSeconds(2))
        if ($null -ne $readback) {
            Write-Host "RTC readback: $readback"
        }
    }
    elseif ($Action -eq 'Read') {
        $serial.WriteLine('READ')
        $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        $received = $false
        while ([datetime]::UtcNow -lt $deadline) {
            $message = Read-SerialLine -Serial $serial -DeadlineUtc $deadline
            if ($null -eq $message) { break }
            if ($message -match '^ERROR:') { throw $message }
            if ($message -match '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$') {
                Write-Output "RTC: $message"
                $received = $true
                break
            }
        }
        if (-not $received) {
            throw "No RTC time received from Arduino on $Port."
        }
    }
    elseif ($Action -eq 'Monitor') {
        Write-Host "Monitoring $Port at 9600 baud. Press Ctrl+C to stop."
        while ($true) {
            $message = Read-SerialLine -Serial $serial -DeadlineUtc ([datetime]::UtcNow.AddSeconds(5))
            if ($null -ne $message) { Write-Host $message }
        }
    }
}
catch [System.UnauthorizedAccessException] {
    throw "Cannot open $Port. Close Arduino Serial Monitor and other programs using the port. Details: $($_.Exception.Message)"
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
