param([int]$Port = 5000)
$ErrorActionPreference = 'Stop'
try {
    $xml = [xml](curl.exe -s -m 5 "http://127.0.0.1:$Port/current")
} catch {
    Write-Host "[ERROR] agent not reachable on port $Port. Start it with start.bat / test.bat"
    exit 1
}
Write-Host ""
Write-Host ("{0,-10} {1,-14} {2,-12} {3}" -f 'Machine','Availability','Execution','Mode')
Write-Host ('-' * 46)
foreach ($d in $xml.MTConnectStreams.Streams.DeviceStream) {
    $n = $d.name; $a = '-'; $e = '-'; $m = '-'
    foreach ($c in $d.ComponentStream) {
        if ($c.component -eq 'Device') {
            if ($c.Events.Availability)   { $a = $c.Events.Availability.'#text' }
            if ($c.Events.Execution)      { $e = $c.Events.Execution.'#text' }
            if ($c.Events.ControllerMode) { $m = $c.Events.ControllerMode.'#text' }
        }
    }
    Write-Host ("{0,-10} {1,-14} {2,-12} {3}" -f $n, $a, $e, $m)
}
