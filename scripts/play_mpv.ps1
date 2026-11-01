param(
    [int]$Port = 1234
)

$RootDir = Split-Path -Parent $PSScriptRoot
$MpvBin = Join-Path $RootDir "tools\mpv\mpv.exe"
$MpvConfDir = Join-Path $RootDir "tools\mpv"

if (-not (Test-Path $MpvBin)) {
    Write-Host "[ERRO] Binario do MPV nao encontrado em $MpvBin." -ForegroundColor Red
    exit 1
}

Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  Iniciando MPV Player com Interpolacao em Tempo Real (ISDB-T 1-Seg)" -ForegroundColor Cyan
Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  - Stream UDP:   udp://127.0.0.1:$Port"
Write-Host "  - Configuracao: $MpvConfDir\mpv.conf"
Write-Host "  - Interpolacao: EWA-Lanczos Sharp (Resolucao) + Display-Resample (Movimento)"
Write-Host ""
Write-Host "Dicas do MPV:" -ForegroundColor Yellow
Write-Host "  [f] - Alternar Tela Cheia"
Write-Host "  [i] - Exibir Estatisticas e Taxa de Quadros (OSD)"
Write-Host "  [d] - Ligar/Desligar Deband"
Write-Host "  [q] - Fechar Player"
Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host ""

Start-Process -FilePath $MpvBin -ArgumentList @("--config-dir=$MpvConfDir", "udp://127.0.0.1:$Port")

