@echo off
setlocal
cd /d "%~dp0\.."

set PORT=%1
if "%PORT%"=="" set PORT=1234

set MPV_BIN=tools\mpv\mpv.exe
set MPV_CONF=tools\mpv\mpv.conf

if not exist "%MPV_BIN%" (
    echo [ERRO] Binario do MPV nao encontrado em %MPV_BIN%.
    pause
    exit /b 1
)

echo ==============================================================================
echo   Iniciando MPV Player com Interpolacao em Tempo Real (ISDB-T 1-Seg)
echo ==============================================================================
echo   - Stream UDP:   udp://127.0.0.1:%PORT%
echo   - Configuracao: %MPV_CONF%
echo   - Interpolacao: EWA-Lanczos Sharp (Resolucao) + Display-Resample (Movimento)
echo.
echo Dicas do MPV:
echo   [f]      - Alternar Tela Cheia
echo   [i]      - Exibir Estatisticas e Taxa de Quadros (OSD)
echo   [d]      - Ligar/Desligar Deband
echo   [q]      - Fechar Player
echo ==============================================================================
echo.

start "" "%MPV_BIN%" --config-dir=tools\mpv udp://127.0.0.1:%PORT%

