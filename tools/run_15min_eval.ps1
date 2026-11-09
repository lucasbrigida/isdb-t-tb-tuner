# Script de orquestração do teste headless de 15 minutos (900s)
Write-Host "[1/4] Disparando MPV headless de telemetria..."
$mpv = Start-Process -FilePath "tools/mpv/x64/mpv.exe" `
  -ArgumentList "udp://@127.0.0.1:1234", "--vo=null", "--ao=null", "--no-terminal", `
                "--log-file=outputs/ch28_15min_sp_cir_eval/mpv_decoder_15min.log" `
  -PassThru

Write-Host "[2/4] MPV iniciado com PID $($mpv.Id). Iniciando receptor ISDB-T (900 segundos)..."
$rxStartTime = Get-Date

& .\build_x64\isdbtb_rx.exe play -c 28 --ofdm 2 --headless --duration 900 `
  --metrics-csv outputs/ch28_15min_sp_cir_eval/metrics_ch28_15min.csv `
  --output-ts outputs/ch28_15min_sp_cir_eval/stream_ch28_15min.ts

$rxEndTime = Get-Date
$durationSec = ($rxEndTime - $rxStartTime).TotalSeconds
Write-Host "[3/4] Receptor ISDB-T finalizado apos $([math]::Round($durationSec, 1)) segundos."

Write-Host "[4/4] Encerrando processo do MPV (PID $($mpv.Id))..."
if ($mpv -and !$mpv.HasExited) {
    Stop-Process -Id $mpv.Id -Force -ErrorAction SilentlyContinue
}
Write-Host "[SUCESSO] Teste de 15 minutos concluido com sucesso!"
