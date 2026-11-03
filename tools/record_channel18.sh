#!/usr/bin/env bash
# ==============================================================================
# record_channel18.sh - Captura idempotente de 2 min do Canal 18 (ISDB-T/ISDB-Tb)
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SAMPLES_DIR="${ROOT_DIR}/samples"
mkdir -p "${SAMPLES_DIR}"

OUTPUT_FILE="${SAMPLES_DIR}/ch18_2min_iq.cu8"
META_FILE="${SAMPLES_DIR}/ch18_2min_iq.meta.json"
SHA_FILE="${SAMPLES_DIR}/ch18_2min_iq.sha256"

# Taxa de amostragem exata do projeto (ver docs/samples/README.md)
SAMPLE_RATE="${ISDBT_SAMPLE_RATE:-2031746}"
if [ "${SAMPLE_RATE}" != "2031746" ]; then
    echo "[ERRO] Taxa de amostragem configurada (${SAMPLE_RATE}) diverge da taxa exata do projeto (2031746 S/s)!" >&2
    exit 1
fi

# Frequência do Canal 18 UHF (Brasil/Japão): 470 + (18 - 14)*6 + 3 + 1/7 MHz = 497.142857 MHz
FREQ_HZ=497142857
GAIN_DB="28.0"
DURATION_SEC=120
TOTAL_SAMPLES=$(( SAMPLE_RATE * DURATION_SEC )) # 243809520 amostras I/Q (487619040 bytes)

# Verificação de idempotência
if [ -f "${OUTPUT_FILE}" ] && [ -f "${SHA_FILE}" ]; then
    echo "[INFO] Arquivo de amostra existente detectado. Verificando integridade SHA-256..."
    EXPECTED_SHA=$(cat "${SHA_FILE}" | awk '{print $1}')
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL_SHA=$(sha256sum "${OUTPUT_FILE}" | awk '{print $1}')
    else
        ACTUAL_SHA=$(shasum -a 256 "${OUTPUT_FILE}" | awk '{print $1}')
    fi

    if [ "${ACTUAL_SHA}" = "${EXPECTED_SHA}" ]; then
        echo "[INFO] Amostra íntegra confirmada (${ACTUAL_SHA}). Captura ignorada (idempotente)."
        exit 0
    else
        echo "[AVISO] Checksum divergiu do esperado. Regravando amostra..."
    fi
fi

# Localiza utilitário rtl_sdr
RTL_SDR_BIN=""
case "$(uname -m 2>/dev/null || echo unknown)" in
    x86_64|amd64) RTL_ARCH="x64" ;;
    i?86|x86) RTL_ARCH="x86" ;;
    *)
        if [ "$(getconf LONG_BIT 2>/dev/null || echo 64)" = "32" ]; then
            RTL_ARCH="x86"
        else
            RTL_ARCH="x64"
        fi
        ;;
esac

FALLBACK_ARCH="x64"
if [ "${RTL_ARCH}" = "x64" ]; then
    FALLBACK_ARCH="x86"
fi

if [ -f "${ROOT_DIR}/tools/rtl-sdr/${RTL_ARCH}/rtl_sdr.exe" ]; then
    RTL_SDR_BIN="${ROOT_DIR}/tools/rtl-sdr/${RTL_ARCH}/rtl_sdr.exe"
elif [ -f "${ROOT_DIR}/tools/rtl-sdr/${FALLBACK_ARCH}/rtl_sdr.exe" ]; then
    RTL_SDR_BIN="${ROOT_DIR}/tools/rtl-sdr/${FALLBACK_ARCH}/rtl_sdr.exe"
elif [ -f "${ROOT_DIR}/tools/rtl-sdr/rtl_sdr.exe" ]; then
    RTL_SDR_BIN="${ROOT_DIR}/tools/rtl-sdr/rtl_sdr.exe"
elif command -v rtl_sdr >/dev/null 2>&1; then
    RTL_SDR_BIN="rtl_sdr"
else
    echo "[ERRO] Utilitário rtl_sdr não encontrado em tools/rtl-sdr/ nem no PATH!" >&2
    exit 1
fi

echo "======================================================================"
echo " Início da Captura de Referência do Canal 18"
echo "======================================================================"
echo " Frequência Central: ${FREQ_HZ} Hz (497.143 MHz)"
echo " Taxa de Amostragem: ${SAMPLE_RATE} S/s"
echo " Ganho RF:           ${GAIN_DB} dB"
echo " Duração:            ${DURATION_SEC} segundos (${TOTAL_SAMPLES} amostras)"
echo " Destino:            ${OUTPUT_FILE}"
echo "======================================================================"

# Executa captura
"${RTL_SDR_BIN}" -f "${FREQ_HZ}" -s "${SAMPLE_RATE}" -g "${GAIN_DB}" -n "${TOTAL_SAMPLES}" "${OUTPUT_FILE}"

# Calcula checksum SHA-256
echo "[INFO] Calculando SHA-256..."
if command -v sha256sum >/dev/null 2>&1; then
    SHA256_HASH=$(sha256sum "${OUTPUT_FILE}" | awk '{print $1}')
else
    SHA256_HASH=$(shasum -a 256 "${OUTPUT_FILE}" | awk '{print $1}')
fi
echo "${SHA256_HASH}  ch18_2min_iq.cu8" > "${SHA_FILE}"

# Grava metadados em JSON
TIMESTAMP_UTC=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
cat <<EOF > "${META_FILE}"
{
  "channel": 18,
  "frequency_hz": ${FREQ_HZ},
  "sample_rate_hz": ${SAMPLE_RATE},
  "sample_rate_source": "include/dsp/constants.hpp:10, AGENTS.md:Invariante 1",
  "gain_db": ${GAIN_DB},
  "duration_seconds": ${DURATION_SEC},
  "total_samples": ${TOTAL_SAMPLES},
  "format": "CU8 (interleaved uint8 I/Q)",
  "recorded_at_utc": "${TIMESTAMP_UTC}",
  "sha256": "${SHA256_HASH}"
}
EOF

echo "[SUCESSO] Captura concluída com sucesso."
echo "  Arquivo:  ${OUTPUT_FILE}"
echo "  SHA-256:  ${SHA256_HASH}"
echo "  Metadados: ${META_FILE}"
