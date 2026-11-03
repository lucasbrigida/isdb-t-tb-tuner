# ==============================================================================
# record_channel18.ps1 - Captura idempotente de 2 min do Canal 18 no Windows
# ==============================================================================
[CmdletBinding()]
param(
    [uint32]$SampleRate = 2031746,
    [double]$GainDb = 28.0,
    [int]$DurationSec = 120
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$SamplesDir = Join-Path $RootDir "samples"

if (-not (Test-Path $SamplesDir)) {
    New-Item -ItemType Directory -Path $SamplesDir | Out-Null
}

$OutputFile = Join-Path $SamplesDir "ch18_2min_iq.cu8"
$MetaFile = Join-Path $SamplesDir "ch18_2min_iq.meta.json"
$ShaFile = Join-Path $SamplesDir "ch18_2min_iq.sha256"

if ($SampleRate -ne 2031746) {
    Write-Error "Taxa de amostragem configurada ($SampleRate) diverge da taxa exata do projeto (2031746 S/s)!"
    exit 1
}

$FreqHz = 497142857
$TotalSamples = [int64]$SampleRate * $DurationSec

# Verificacao de idempotencia
if ((Test-Path $OutputFile) -and (Test-Path $ShaFile)) {
    Write-Host "[INFO] Arquivo de amostra existente detectado. Verificando integridade SHA-256..."
    $ExpectedSha = (Get-Content $ShaFile).Split(" ")[0].Trim()
    $ActualSha = (Get-FileHash -Path $OutputFile -Algorithm SHA256).Hash.ToLower()

    if ($ActualSha -eq $ExpectedSha.ToLower()) {
        Write-Host "[INFO] Amostra integra confirmada ($ActualSha). Captura ignorada (idempotente)." -ForegroundColor Green
        exit 0
    } else {
        Write-Warning "Checksum divergiu do esperado ($ActualSha != $ExpectedSha). Regravando amostra..."
    }
}

$RtlSdrBin = Join-Path $RootDir "tools\rtl-sdr\x64\rtl_sdr.exe"
if (-not (Test-Path $RtlSdrBin)) {
    $RtlSdrBin = Join-Path $RootDir "tools\rtl-sdr\x86\rtl_sdr.exe"
}
if (-not (Test-Path $RtlSdrBin)) {
    $RtlSdrBin = Join-Path $RootDir "tools\rtl-sdr\rtl_sdr.exe"
}
if (-not (Test-Path $RtlSdrBin)) {
    Write-Error "Utilitario rtl_sdr.exe nao encontrado em tools/rtl-sdr/x64 nem em tools/rtl-sdr/x86!"
    exit 1
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Inicio da Captura de Referencia do Canal 18" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Frequencia Central: $FreqHz Hz (497.143 MHz)"
Write-Host " Taxa de Amostragem: $SampleRate S/s"
Write-Host " Ganho RF:           $GainDb dB"
Write-Host " Duracao:            $DurationSec segundos ($TotalSamples amostras)"
Write-Host " Destino:            $OutputFile"
Write-Host "======================================================================" -ForegroundColor Cyan

# Executa captura
& $RtlSdrBin -f $FreqHz -s $SampleRate -g $GainDb -n $TotalSamples $OutputFile

if ($LASTEXITCODE -ne 0) {
    Write-Error "Falha na execucao do rtl_sdr.exe (ExitCode: $LASTEXITCODE)"
    exit 1
}

Write-Host "[INFO] Calculando SHA-256..."
$Sha256Hash = (Get-FileHash -Path $OutputFile -Algorithm SHA256).Hash.ToLower()
"$Sha256Hash  ch18_2min_iq.cu8" | Set-Content -Path $ShaFile -Encoding ASCII

$TimestampUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$MetaJson = @"
{
  "channel": 18,
  "frequency_hz": $FreqHz,
  "sample_rate_hz": $SampleRate,
  "sample_rate_source": "include/dsp/constants.hpp:10, AGENTS.md:Invariante 1",
  "gain_db": $GainDb,
  "duration_seconds": $DurationSec,
  "total_samples": $TotalSamples,
  "format": "CU8 (interleaved uint8 I/Q)",
  "recorded_at_utc": "$TimestampUtc",
  "sha256": "$Sha256Hash"
}
"@

$MetaJson | Set-Content -Path $MetaFile -Encoding UTF8

Write-Host "[SUCESSO] Captura concluida com sucesso." -ForegroundColor Green
Write-Host "  Arquivo:   $OutputFile"
Write-Host "  SHA-256:   $Sha256Hash"
Write-Host "  Metadados: $MetaFile"

