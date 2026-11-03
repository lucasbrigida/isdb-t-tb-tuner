$bytes = [System.IO.File]::ReadAllBytes("test_capture_15s.ts")
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $ts_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($ts_pid -eq 0x0000) {
        $hex = ($bytes[$i..($i+31)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
        Write-Host "PAT TS Packet: $hex"
        break
    }
}
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $ts_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($ts_pid -eq 0x1FC8) {
        $hex = ($bytes[$i..($i+31)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
        Write-Host "PMT (0x1FC8) TS Packet: $hex"
        break
    }
}

