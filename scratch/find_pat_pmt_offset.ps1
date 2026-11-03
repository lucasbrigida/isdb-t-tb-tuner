$bytes = [System.IO.File]::ReadAllBytes("test_capture_15s.ts")
$count = 0
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $t_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($t_pid -eq 0x0000) {
        $count++
        Write-Host ("PAT #{0} is at byte {1} (TS packet #{2})" -f $count, $i, ($i/188))
        if ($count -eq 5) { break }
    }
}
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $t_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($t_pid -eq 0x1FC8) {
        Write-Host ("First PMT (0x1FC8) is at byte {0} (TS packet #{1})" -f $i, ($i/188))
        break
    }
}

