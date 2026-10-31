# Runner de Testes Automatizados para todos os executáveis em build/
$ErrorActionPreference = "Continue"

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  EXECUTANDO A SUÍTE DE TESTES INTEGRADA EM BUILD/      " -ForegroundColor Cyan
Write-Host "========================================================`n" -ForegroundColor Cyan

python tests\test_all_executables.py
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "`n[SUCESSO] Todos os testes passaram com êxito!`n" -ForegroundColor Green
} else {
    Write-Host "`n[FALHA] Alguns testes falharam (código $exitCode).`n" -ForegroundColor Red
}

exit $exitCode

