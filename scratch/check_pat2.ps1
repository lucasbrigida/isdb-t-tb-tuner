$bytes = [System.IO.File]::ReadAllBytes("test_capture_15s.ts")
$count = 0
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $t_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($t_pid -eq 0x0000) {
        $count++
        if ($count -eq 2) {
            $hex = ($bytes[$i..($i+25)] | ForEach-Object { "{0:X2}" -f $_ }) -join " "
            Write-Host "PAT Packet #2: $hex"
            break
        }
    }
}

