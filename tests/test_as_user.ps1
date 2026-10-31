# Simula fielmente a experiencia do usuario no terminal PowerShell
$ErrorActionPreference = "Continue"

function Assert-CommandSuccess {
    param(
        [string]$Description,
        [scriptblock]$Script
    )
    Write-Host "[TESTE TERMINAL] $Description ... " -NoNewline -ForegroundColor Cyan
    try {
        $output = & $Script
        $code = $LASTEXITCODE
        if ($code -eq 0) {
            Write-Host "PASS (OK)" -ForegroundColor Green
        } else {
            Write-Host "FAIL (Codigo $code)" -ForegroundColor Red
            Write-Host $output -ForegroundColor DarkYellow
            exit 1
        }
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
}

Write-Host "`n========================================================" -ForegroundColor Yellow
Write-Host "  TESTES DE TERMINAL PURO (EXPERIENCIA REAL DO USUARIO) " -ForegroundColor Yellow
Write-Host "========================================================`n" -ForegroundColor Yellow

# 1. Teste do Probe
Assert-CommandSuccess "Executar .\build\isdbtb_rx.exe probe" {
    .\build\isdbtb_rx.exe probe
}

# 2. Teste do Help
Assert-CommandSuccess "Executar .\build\isdbtb_rx.exe --help" {
    .\build\isdbtb_rx.exe --help
}

# 3. Teste do Scan (canais 28 ao 30)
Assert-CommandSuccess "Executar .\build\isdbtb_rx.exe scan --start 28 --end 30" {
    .\build\isdbtb_rx.exe scan --start 28 --end 30
}

# 4. Teste do List
Assert-CommandSuccess "Executar .\build\isdbtb_rx.exe list" {
    .\build\isdbtb_rx.exe list
}

# 5. Teste do Export
Assert-CommandSuccess "Executar .\build\isdbtb_rx.exe export -o build\user_test.m3u" {
    .\build\isdbtb_rx.exe export -o build\user_test.m3u
}

# 6. Testes dos binarios de teste do DSP
Assert-CommandSuccess "Executar .\build\test_reed_solomon.exe" {
    .\build\test_reed_solomon.exe
}

Assert-CommandSuccess "Executar .\build\test_descrambler.exe" {
    .\build\test_descrambler.exe
}

Assert-CommandSuccess "Executar .\build\test_viterbi.exe" {
    .\build\test_viterbi.exe
}

Assert-CommandSuccess "Executar .\build\test_fec_pipeline.exe" {
    .\build\test_fec_pipeline.exe
}

Assert-CommandSuccess "Executar .\build\test_ofdm_sync.exe" {
    .\build\test_ofdm_sync.exe
}

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "  TODOS OS COMANDOS DO USUARIO NO TERMINAL FUNCIONAM!   " -ForegroundColor Green
Write-Host "========================================================`n" -ForegroundColor Green
