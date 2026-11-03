$bytes = [System.IO.File]::ReadAllBytes("test_capture_15s.ts")
$pat_count = 0
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $ts_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if ($ts_pid -eq 0x0000) {
        $pat_count++
        $prog = ($bytes[$i+13] -shl 8) -bor $bytes[$i+14]
        $ver = ($bytes[$i+10] -shr 1) -band 0x1F
        Write-Host ("PAT #{0}: prog=0x{1:X4} ({1}) ver={2}" -f $pat_count, $prog, $ver)
    }
}

