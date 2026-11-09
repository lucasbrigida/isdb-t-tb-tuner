# Diretrizes Absolutas para Agentes de IA
❌ = NUNCA. Obrigatórias.

1. ❌ alterar a taxa para 2.048 MSps. Sempre `2031746 S/s` (`isdbt::constants::SAMPLE_RATE`).
2. ❌ ler RTL-SDR síncrono no loop de DSP. Captura USB em thread isolado com `SpscCaptureQueue` (evita overflow USB).
3. ❌ enviar pacotes de 188 B soltos via UDP. Sempre 7 pacotes = 1316 B (`constants::UDP_PACKET_SIZE`).
4. ❌ alterar a URL local: `udp://@127.0.0.1:1234` (VLC), `udp://127.0.0.1:1234` (MPV).
5. ❌ buffers > 64 KB ou reload IPC no canal inicial do MPV. 1-Seg ≈ 312 kbps; 1 MB trava dezenas de segundos; reload precoce aborta o demuxer (tela preta).
6. ❌ injetar pacotes sintéticos (`TsGenerator`) em fluxo real (tela verde trava os PIDs do player).
7. ❌ limiar de IFO < 0.60 sem histerese (lóbulos de SP têm coerência ~0.35 e prendem em frequência falsa).
8. ❌ reiniciar a treliça do Viterbi a cada símbolo OFDM (fluxo convolucional contínuo).
9. ❌ remover o delay de 100 ms após sintonizar (PLL R820T; senão `[R82XX] PLL not locked!`).
10. ❌ mudar frequência sem `rtlsdr_reset_buffer` + espera do PLL. `rtlsdr_set_center_freq` após leituras sem reset → `LIBUSB_ERROR_PIPE -9`, I2C do R820T trava.
11. ❌ `std::deque` ou alocação dinâmica no loop de símbolos. Usar `FixedDelayLine`/`DynamicDelayLine` pré-alocadas.
12. ❌ aplicar CSI antes do demapeador ou desvincular pesos das linhas de retardo de bits. CSI só sobre soft-bits centrados em 128; pesos com os mesmos atrasos (120 símbolos no MSB).
13. ❌ trocar a fila SPSC lock-free por mutex ou ignorar gaps de sequência sem reset coordenado (`reset_play_reception_stream` / `reset_scan_reception`) (descontinuidade de fase no NCO).
14. ❌ modificar o caminho de execução de `src/dsp/ofdm_sync.cpp` (`VanDeBeekSync`, baseline).
15. ❌ rejeitar `--ofdm` fora de `play`; em `scan`/`probe` aceitar e descartar em silêncio.
16. ❌ remover `reload_mpv_player(udp_port)` na troca de canal (MPV precisa de `loadfile replace` via IPC para rearmar demuxers/decoders na mesma janela).
17. ❌ fragmentar `send_discontinuity_burst()`: exatamente 7 × 188 B no PID `0x0000` com flag de descontinuidade (1316 B atômicos).
18. ❌ alterar documentação para justificar simplificações. `SpCirSynchronizer` (port gr-isdbt) com SP deve ser preservado.
19. ❌ testes CTest com sockets reais/bind local. Testar datagramas e pacotes TS de forma determinística em memória.
20. ❌ deixar badges de teste de README.md e README_en.md desatualizados. Ao criar/alterar/excluir/executar teste: conferir `ctest -N` e o resultado, atualizar IMEDIATAMENTE os badges (`Tests-<passed>%2F<total>%20Passed-success.svg`; `critical` se houver falha).
21. ❌ forçar `ofdm_locked = true`, pausar `reader_thread` sem drenar USB com fila cheia, ou omitir watchdog TMCC/TS. `ofdm_locked` = `SyncState::Tracking`; fila cheia → ler e descartar no hardware (mantém 2031746 S/s, sem represamento no kernel); > 3 s sem TMCC lock após início → re-aquisição imediata.
