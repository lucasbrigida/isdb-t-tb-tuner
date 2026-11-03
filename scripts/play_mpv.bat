@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

set PORT=1234
set TARGET_ARCH=auto

rem Parsing flexivel de argumentos (%1 e %2)
if /i "%~1"=="x86" (
    set TARGET_ARCH=x86
) else if /i "%~1"=="32" (
    set TARGET_ARCH=x86
) else if /i "%~1"=="x64" (
    set TARGET_ARCH=x64
) else if /i "%~1"=="64" (
    set TARGET_ARCH=x64
) else if not "%~1"=="" (
    set PORT=%~1
)

if /i "%~2"=="x86" (
    set TARGET_ARCH=x86
) else if /i "%~2"=="32" (
    set TARGET_ARCH=x86
) else if /i "%~2"=="x64" (
    set TARGET_ARCH=x64
) else if /i "%~2"=="64" (
    set TARGET_ARCH=x64
)

set MPV_BIN=tools\mpv\mpv.exe
set MPV_CONF_DIR=tools\mpv

if "%TARGET_ARCH%"=="x86" (
    if exist "tools\mpv\x86\mpv.exe" (
        set MPV_BIN=tools\mpv\x86\mpv.exe
        set MPV_CONF_DIR=tools\mpv\x86
    )
) else if "%TARGET_ARCH%"=="auto" (
    if exist "tools\mpv\x86\mpv.exe" (
        if "%PROCESSOR_ARCHITECTURE%"=="x86" (
            set MPV_BIN=tools\mpv\x86\mpv.exe
            set MPV_CONF_DIR=tools\mpv\x86
        )
    )
)

if not exist "%MPV_BIN%" (
    echo [ERRO] Binario do MPV nao encontrado em %MPV_BIN%.
    pause
    exit /b 1
)

echo ==============================================================================
echo   Iniciando MPV Player para TV Digital (ISDB-T/ISDB-Tb 1-Seg)
echo ==============================================================================
echo   - Stream UDP:   udp://127.0.0.1:%PORT%
echo   - Binario:      %MPV_BIN%
echo   - Configuracao: %MPV_CONF_DIR%\mpv.conf
echo.
echo Dicas do MPV:
echo   [f]      - Alternar Tela Cheia
echo   [i]      - Exibir Estatisticas e Taxa de Quadros (OSD)
echo   [d]      - Ligar/Desligar Deband
echo   [q]      - Fechar Player
echo ==============================================================================
echo.

start "" "%MPV_BIN%" --config-dir="%MPV_CONF_DIR%" --idle=yes "udp://127.0.0.1:%PORT%?localaddr=127.0.0.1"
