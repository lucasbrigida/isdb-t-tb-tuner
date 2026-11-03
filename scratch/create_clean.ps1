$bytes = [System.IO.File]::ReadAllBytes("test_capture_15s.ts")
# Skip first 29 packets (5452 bytes)
$clean_bytes = New-Object byte[] ($bytes.Length - 5452)
[System.Array]::Copy($bytes, 5452, $clean_bytes, 0, $clean_bytes.Length)
[System.IO.File]::WriteAllBytes("test_clean_pat.ts", $clean_bytes)
Write-Host "Created test_clean_pat.ts starting at PAT #2 (prog 0xE778)"

