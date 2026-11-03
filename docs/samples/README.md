# Amostras de Referência RF (IQ) - ISDB-T / ISDB-Tb

Este diretório e documento detalham a metodologia de obtenção, parâmetros físicos e rastreabilidade das amostras brutas de I/Q utilizadas nos testes de regressão e benchmarks do receptor.

---

## 1. Taxa de Amostragem Oficial do Projeto

A taxa de amostragem exata utilizada no pipeline físico é de **2031746 S/s** (aproximadamente `2.031746 MSps`).

### Fontes Consultadas no Repositório:
1. `include/dsp/constants.hpp` (Linhas 9-10):
   ```cpp
   constexpr float SAMPLE_RATE_HZ = 2031746.031746f;
   constexpr uint32_t SAMPLE_RATE = 2031746;
   ```
2. `AGENTS.md` (Seção 2, Invariante 1):
   - A taxa base do padrão ISDB-T é $F_{\text{base}} = \frac{512}{63}\text{ MHz} \approx 8.126984\text{ MHz}$.
   - Para recepção 1-Seg com RTL-SDR, a taxa é o submúltiplo exato de $1/4$:
     $$F_s = \frac{F_{\text{base}}}{4} = \frac{512}{252}\text{ MHz} = 2031746\text{ Hz}$$
   - Essa taxa alinha perfeitamente 2048 pontos por símbolo OFDM no Modo 3 e garante a correlação ideal do prefixo cíclico.
3. `src/cli/cmd_play.cpp` (Linha 156) e `src/cli/cmd_scan.cpp` (Linha 93):
   ```cpp
   constexpr uint32_t sample_rate = constants::SAMPLE_RATE;
   rtlsdr_set_sample_rate(dev, sample_rate);
   ```

---

## 2. Parâmetros de Sintonia do Canal 18 (UHF Brasil)

- **Número do canal**: 18
- **Fórmula do plano de frequência** (ABNT NBR 15601 e `src/radio/frequency_plan.cpp`):
  $$F_c = 470\text{ MHz} + (\text{canal} - 14) \times 6\text{ MHz} + 3\text{ MHz} + \frac{1}{7}\text{ MHz}$$
  $$F_c = 470 + (18 - 14) \times 6 + 3.142857 = 497.142857\text{ MHz} = 497142857\text{ Hz}$$
- **Duração da gravação**: 120 segundos (2 minutos contínuos)
- **Formato**: Amostras I/Q de 8 bits não sinalizados (`CU8`, interleaved `I` e `Q` entre 0 e 255)
- **Ganho do sintonizador**: 28.0 dB (ganho fixo calibrado para prevenir saturação do ADC)

---

## 3. Instruções de Captura Idempotente

Para capturar a amostra real utilizando o dongle RTL-SDR conectado:

```bash
# No Linux / macOS / Git Bash:
tools/record_channel18.sh

# No Windows PowerShell:
.\tools\record_channel18.ps1
```

O script verifica se o arquivo `samples/ch18_2min_iq.cu8` já existe e se o hash SHA-256 coincide com `samples/ch18_2min_iq.sha256`. Se for válido, a captura é ignorada com sucesso (idempotência).

---

## 4. Política de Versionamento e Armazenamento (Git LFS)

- Arquivos brutos `.cu8` (~487 MB para 120s a 2.031 MSps) são versionados e rastreados através do **Git LFS** (`samples/*.cu8` em `.gitattributes`).
- O sidecar de metadados `samples/ch18_2min_iq.meta.json` e o checksum `samples/ch18_2min_iq.sha256` são commitados para garantir a reprodutibilidade dos testes de regressão.

