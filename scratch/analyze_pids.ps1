$bytes = [System.IO.File]::ReadAllBytes("test_capture_real.ts")
$pids = @{}
for ($i = 0; $i -lt $bytes.Length; $i += 188) {
    if ($bytes[$i] -ne 0x47) { continue }
    $ts_pid = (($bytes[$i+1] -band 0x1F) -shl 8) -bor $bytes[$i+2]
    if (-not $pids.ContainsKey($ts_pid)) { $pids[$ts_pid] = 0 }
    $pids[$ts_pid]++
}
$pids.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    "PID 0x{0:X4} ({0}): {1} pkts" -f $_.Key, $_.Value
}
