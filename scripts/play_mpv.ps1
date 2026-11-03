param(
    [int]$Port = 1234,
    [ValidateSet('auto', 'x64', 'x86', '64', '32')]
    [string]$Arch = 'auto'
)

$RootDir = Split-Path -Parent $PSScriptRoot
$MpvBin = Join-Path $RootDir "tools\mpv\x64\mpv.exe"
$MpvConfDir = Join-Path $RootDir "tools\mpv\x64"

if (-not (Test-Path $MpvBin)) {
    $MpvBin = Join-Path $RootDir "tools\mpv\mpv.exe"
    $MpvConfDir = Join-Path $RootDir "tools\mpv"
}

# Deteccao automatica ou forçada de ambiente 32-bit (x86)
$is32Bit = ($Arch -in @('x86', '32')) -or ($Arch -eq 'auto' -and ([IntPtr]::Size -eq 4 -or $env:PROCESSOR_ARCHITECTURE -eq 'x86'))

if ($is32Bit -and (Test-Path (Join-Path $RootDir "tools\mpv\x86\mpv.exe"))) {
    $MpvBin = Join-Path $RootDir "tools\mpv\x86\mpv.exe"
    $MpvConfDir = Join-Path $RootDir "tools\mpv\x86"
}

if (-not (Test-Path $MpvBin)) {
    Write-Host "[ERRO] Binario do MPV nao encontrado em $MpvBin." -ForegroundColor Red
    exit 1
}

Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  Iniciando MPV Player para TV Digital (ISDB-T/ISDB-Tb 1-Seg)" -ForegroundColor Cyan
Write-Host "==============================================================================`n" -ForegroundColor Cyan
Write-Host "  - Stream UDP:   udp://127.0.0.1:$Port"
Write-Host "  - Binario:      $MpvBin"
Write-Host "  - Configuracao: $MpvConfDir\mpv.conf"
Write-Host ""
Write-Host "Dicas do MPV:" -ForegroundColor Yellow
Write-Host "  [f] - Alternar Tela Cheia"
Write-Host "  [i] - Exibir Estatisticas e Taxa de Quadros (OSD)"
Write-Host "  [d] - Ligar/Desligar Deband"
Write-Host "  [q] - Fechar Player"
Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host ""

Start-Process -FilePath $MpvBin -ArgumentList @("--config-dir=$MpvConfDir", "--idle=yes", "udp://127.0.0.1:$Port?localaddr=127.0.0.1")
