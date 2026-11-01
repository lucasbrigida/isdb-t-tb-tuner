#pragma once
#include <string>

namespace isdbt {

inline const std::string WEB_UI_HTML = R"rawhtml(<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ISDB-T/Tb Web Tuner & Player</title>
  <!-- hls.js para decodificacao de Live HLS H.264/AAC no navegador -->
  <script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.8/dist/hls.min.js"></script>
  <style>
    :root {
      --bg-dark: #0f141c;
      --card-bg: #182230;
      --card-border: #26384d;
      --accent-blue: #00b4d8;
      --accent-green: #00f5d4;
      --accent-red: #ff4d6d;
      --accent-yellow: #fee440;
      --text-main: #f0f4f8;
      --text-muted: #8da4be;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    body { background-color: var(--bg-dark); color: var(--text-main); min-height: 100vh; display: flex; flex-direction: column; }

    header {
      background: linear-gradient(90deg, #131c28, #1c2b3d);
      border-bottom: 1px solid var(--card-border);
      padding: 1rem 2rem;
      display: flex;
      justify-content: space-between;
      align-items: center;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    }
    .logo-container { display: flex; align-items: center; gap: 0.75rem; }
    .logo-icon { font-size: 1.8rem; }
    .logo-text h1 { font-size: 1.25rem; font-weight: 700; color: #fff; letter-spacing: 0.5px; }
    .logo-text span { font-size: 0.8rem; color: var(--accent-blue); text-transform: uppercase; font-weight: 600; }

    .header-actions { display: flex; gap: 1rem; align-items: center; }
    .btn {
      padding: 0.5rem 1rem;
      border: none;
      border-radius: 6px;
      font-size: 0.9rem;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      transition: all 0.2s ease;
    }
    .btn-primary { background: linear-gradient(135deg, #0077b6, #00b4d8); color: #fff; }
    .btn-primary:hover { background: linear-gradient(135deg, #0096c7, #48cae4); box-shadow: 0 0 10px rgba(0,180,216,0.4); }
    .btn-danger { background: #d90429; color: #fff; }
    .btn-danger:hover { background: #ef233c; box-shadow: 0 0 10px rgba(239,35,60,0.4); }
    .btn-secondary { background: #26384d; color: #fff; }
    .btn-secondary:hover { background: #354e6b; }

    .status-badge {
      display: inline-flex;
      align-items: center;
      gap: 0.4rem;
      padding: 0.35rem 0.8rem;
      border-radius: 20px;
      font-size: 0.8rem;
      font-weight: 700;
      text-transform: uppercase;
      background: rgba(0, 245, 212, 0.1);
      color: var(--accent-green);
      border: 1px solid rgba(0, 245, 212, 0.3);
    }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--accent-green); animation: pulse 2s infinite; }
    .status-badge.stopped { background: rgba(255, 77, 109, 0.1); color: var(--accent-red); border-color: rgba(255, 77, 109, 0.3); }
    .status-badge.stopped .status-dot { background: var(--accent-red); animation: none; }
    .status-badge.scanning { background: rgba(254, 228, 64, 0.1); color: var(--accent-yellow); border-color: rgba(254, 228, 64, 0.3); }
    .status-badge.scanning .status-dot { background: var(--accent-yellow); animation: pulse 1s infinite; }

    @keyframes pulse {
      0% { transform: scale(0.95); opacity: 0.7; }
      50% { transform: scale(1.2); opacity: 1; }
      100% { transform: scale(0.95); opacity: 0.7; }
    }

    main {
      flex: 1;
      padding: 1.5rem 2rem;
      display: grid;
      grid-template-columns: 1fr 380px;
      gap: 1.5rem;
      max-width: 1600px;
      margin: 0 auto;
      width: 100%;
    }

    @media (max-width: 1024px) {
      main { grid-template-columns: 1fr; }
    }

    .video-section { display: flex; flex-direction: column; gap: 1rem; }
    .video-container {
      position: relative;
      background: #000;
      border-radius: 12px;
      overflow: hidden;
      aspect-ratio: 16 / 9;
      box-shadow: 0 8px 24px rgba(0,0,0,0.5);
      border: 1px solid var(--card-border);
    }
    video { width: 100%; height: 100%; object-fit: contain; }

    .telemetry-bar {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 1rem;
    }
    @media (max-width: 600px) {
      .telemetry-bar { grid-template-columns: repeat(2, 1fr); }
    }
    .metric-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 0.8rem 1rem;
    }
    .metric-title { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; font-weight: 600; margin-bottom: 0.25rem; }
    .metric-value { font-size: 1.15rem; font-weight: 700; color: #fff; }
    .signal-bar-wrap { height: 6px; background: #26384d; border-radius: 3px; margin-top: 0.4rem; overflow: hidden; }
    .signal-bar-fill { height: 100%; width: 0%; background: var(--accent-green); transition: width 0.3s; }

    .channels-section { display: flex; flex-direction: column; gap: 1rem; }
    .section-header { display: flex; justify-content: space-between; align-items: center; }
    .section-title { font-size: 1.1rem; font-weight: 700; color: #fff; }

    .channels-grid {
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
      max-height: 520px;
      overflow-y: auto;
      padding-right: 0.5rem;
    }
    .channel-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 0.85rem 1rem;
      display: flex;
      justify-content: space-between;
      align-items: center;
      transition: all 0.2s ease;
      cursor: pointer;
    }
    .channel-card:hover {
      border-color: var(--accent-blue);
      transform: translateX(3px);
      background: #1c2838;
    }
    .channel-card.active {
      border-color: var(--accent-green);
      background: rgba(0, 245, 212, 0.05);
    }
    .ch-left { display: flex; align-items: center; gap: 0.8rem; }
    .ch-number {
      background: #26384d;
      color: #fff;
      font-weight: 800;
      font-size: 1rem;
      width: 38px;
      height: 38px;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .channel-card.active .ch-number { background: var(--accent-green); color: #000; }
    .ch-info h4 { font-size: 0.95rem; font-weight: 600; color: #fff; }
    .ch-info p { font-size: 0.75rem; color: var(--text-muted); }

    .btn-play-sm {
      background: #0077b6;
      color: #fff;
      border: none;
      padding: 0.4rem 0.8rem;
      border-radius: 6px;
      font-size: 0.8rem;
      font-weight: 600;
      cursor: pointer;
    }
    .btn-play-sm:hover { background: #0096c7; }

    .log-box {
      background: #080c12;
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 0.8rem;
      font-family: "Cascadia Code", Consolas, monospace;
      font-size: 0.75rem;
      color: #90e0ef;
      height: 140px;
      overflow-y: auto;
      white-space: pre-wrap;
    }

    .modal-overlay {
      position: fixed;
      inset: 0;
      background: rgba(0,0,0,0.7);
      display: none;
      justify-content: center;
      align-items: center;
      z-index: 100;
    }
    .modal-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 1.5rem;
      width: 90%;
      max-width: 500px;
    }
    .modal-card h3 { margin-bottom: 1rem; }
    .progress-bar-wrap { height: 10px; background: #26384d; border-radius: 5px; overflow: hidden; margin-bottom: 1rem; }
    .progress-bar-fill { height: 100%; width: 100%; background: var(--accent-blue); animation: progress-indeterminate 1.5s infinite linear; }
    @keyframes progress-indeterminate {
      0% { transform: translateX(-100%); }
      100% { transform: translateX(100%); }
    }
  </style>
</head>
<body>

  <header>
    <div class="logo-container">
      <div class="logo-icon">📺</div>
      <div class="logo-text">
        <h1>ISDB-T / ISDB-Tb Web Player</h1>
        <span>Digital TV SDR Tuner & Streamer</span>
      </div>
    </div>
    <div class="header-actions">
      <div id="statusBadge" class="status-badge stopped">
        <div class="status-dot"></div>
        <span id="statusText">Parado</span>
      </div>
      <button class="btn btn-primary" onclick="startScan()">🔍 Buscar Canais</button>
      <button class="btn btn-danger" onclick="stopPlayback()">⏹️ Parar</button>
    </div>
  </header>

  <main>
    <!-- Coluna Esquerda: Player e Telemetria -->
    <div class="video-section">
      <div class="video-container">
        <video id="videoPlayer" controls autoplay muted playsinline></video>
      </div>

      <div class="telemetry-bar">
        <div class="metric-card">
          <div class="metric-title">Nível de RF (RSSI)</div>
          <div id="metricRssi" class="metric-value">-- dBFS</div>
          <div class="signal-bar-wrap">
            <div id="signalBar" class="signal-bar-fill"></div>
          </div>
        </div>
        <div class="metric-card">
          <div class="metric-title">Sincronismo OFDM</div>
          <div id="metricOfdm" class="metric-value">--</div>
        </div>
        <div class="metric-card">
          <div class="metric-title">Quadro TMCC</div>
          <div id="metricTmcc" class="metric-value">--</div>
        </div>
        <div class="metric-card">
          <div class="metric-title">Pacotes MPEG-TS</div>
          <div id="metricTs" class="metric-value">0</div>
        </div>
      </div>

      <div style="display: flex; gap: 0.75rem; flex-wrap: wrap; align-items: center; background: var(--card-bg); padding: 0.75rem 1rem; border-radius: 8px; border: 1px solid var(--card-border);">
        <button class="btn btn-primary" style="font-size: 0.85rem; padding: 0.4rem 0.9rem;" onclick="launchVlc()">📺 Abrir no VLC (Vídeo + Áudio Nativo)</button>
        <button class="btn btn-secondary" style="font-size: 0.85rem; padding: 0.4rem 0.9rem;" onclick="reloadPlayer()">🔄 Recarregar Vídeo Web</button>
        <span style="font-size: 0.8rem; color: var(--text-muted); margin-left: auto;">* Áudio AAC estéreo integrado no navegador + compatibilidade nativa no VLC</span>
      </div>

      <div>
        <div class="section-title" style="margin-bottom: 0.5rem; font-size: 0.9rem;">Terminal de Telemetria (stdio)</div>
        <div id="logBox" class="log-box">Aguardando comando...</div>
      </div>
    </div>

    <!-- Coluna Direita: Grade de Canais -->
    <div class="channels-section">
      <div class="section-header">
        <div class="section-title">Canais Sintonizados</div>
        <button class="btn btn-secondary" style="font-size: 0.8rem; padding: 0.25rem 0.6rem;" onclick="loadChannels()">Atualizar</button>
      </div>

      <div id="channelsList" class="channels-grid">
        <p style="color: var(--text-muted); font-size: 0.85rem;">Carregando lista de canais...</p>
      </div>
    </div>
  </main>

  <script>
    let hlsInstance = null;
    let currentChannel = 0;

    function initPlayer() {
      const videoElement = document.getElementById('videoPlayer');
      if (hlsInstance) {
        hlsInstance.destroy();
        hlsInstance = null;
      }

      videoElement.muted = true; // Permite autoplay sem bloqueio do navegador

      if (window.Hls && Hls.isSupported()) {
        hlsInstance = new Hls({
          liveSyncDurationCount: 2,
          liveMaxLatencyDurationCount: 4,
          maxBufferLength: 6,
          manifestLoadingMaxRetry: 10,
          manifestLoadingRetryDelay: 1000,
          manifestLoadingMaxRetryTimeout: 15000,
          enableWorker: true,
          lowLatencyMode: true
        });

        hlsInstance.loadSource('/hls/live.m3u8');
        hlsInstance.attachMedia(videoElement);

        hlsInstance.on(Hls.Events.MANIFEST_PARSED, function () {
          videoElement.play().catch(e => {
            console.log('Autoplay aguardando interacao:', e);
          });
        });

        hlsInstance.on(Hls.Events.ERROR, function (event, data) {
          if (data.fatal) {
            switch (data.type) {
              case Hls.ErrorTypes.NETWORK_ERROR:
                // Tenta recarregar se o HLS ainda estiver gerando os primeiros fragmentos
                setTimeout(() => {
                  if (hlsInstance) {
                    hlsInstance.loadSource('/hls/live.m3u8');
                    hlsInstance.startLoad();
                  }
                }, 1500);
                break;
              case Hls.ErrorTypes.MEDIA_ERROR:
                hlsInstance.recoverMediaError();
                break;
              default:
                hlsInstance.destroy();
                hlsInstance = null;
                break;
            }
          }
        });
      } else if (videoElement.canPlayType('application/vnd.apple.mpegurl')) {
        videoElement.src = '/hls/live.m3u8';
        videoElement.play().catch(e => console.log('Autoplay:', e));
      }
    }

    async function launchVlc() {
      try {
        const res = await fetch('/api/vlc', { method: 'POST' });
        const data = await res.json();
        if (!data.success) {
          alert('VLC nao encontrado automaticamente. Abra o VLC manualmente e conecte em: udp://@127.0.0.1:1234');
        }
      } catch (err) {
        console.error('Erro ao abrir VLC:', err);
      }
    }

    function reloadPlayer() {
      initPlayer();
    }

    async function loadChannels() {
      try {
        const res = await fetch('/api/channels');
        const data = await res.json();
        const listEl = document.getElementById('channelsList');
        listEl.innerHTML = '';

        if (!data.channels || data.channels.length === 0) {
          listEl.innerHTML = '<p style="color: var(--text-muted); font-size: 0.85rem;">Nenhum canal sintonizado ainda. Clique em "Buscar Canais" acima!</p>';
          return;
        }

        data.channels.forEach(ch => {
          const card = document.createElement('div');
          card.className = 'channel-card' + (ch.channel === currentChannel ? ' active' : '');
          const safeName = (ch.name || '').replace(/'/g, "\\'");
          card.onclick = () => playChannel(ch.channel, ch.name);

          const freqMhz = (ch.frequency_hz / 1e6).toFixed(3);
          const pwrDbfs = ch.rssi_dbfs ? ch.rssi_dbfs.toFixed(1) + ' dBFS' : '--';

          card.innerHTML = `
            <div class="ch-left">
              <div class="ch-number">${ch.channel}</div>
              <div class="ch-info">
                <h4>${ch.name}</h4>
                <p>${freqMhz} MHz | ${pwrDbfs}</p>
              </div>
            </div>
            <button class="btn-play-sm" onclick="event.stopPropagation(); playChannel(${ch.channel}, '${safeName}')">Assistir</button>
          `;
          listEl.appendChild(card);
        });
      } catch (err) {
        console.error('Falha ao carregar canais:', err);
      }
    }

    async function playChannel(ch, name) {
      if (!ch && !name) {
        console.error('Nenhum canal selecionado');
        return;
      }
      currentChannel = ch;
      loadChannels();
      try {
        const url = '/api/play?channel=' + encodeURIComponent(ch) + '&name=' + encodeURIComponent(name || '');
        await fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ channel: ch, name: name })
        });
        setTimeout(initPlayer, 2000);
      } catch (err) {
        alert('Erro ao sintonizar canal: ' + err);
      }
    }

    async function stopPlayback() {
      currentChannel = 0;
      loadChannels();
      if (hlsInstance) {
        hlsInstance.destroy();
        hlsInstance = null;
      }
      try {
        await fetch('/api/stop', { method: 'POST' });
      } catch (err) {
        console.error(err);
      }
    }

    async function startScan() {
      if (!confirm('Iniciar varredura profunda de canais UHF (14 a 45)? A reproducao atual sera interrompida.')) {
        return;
      }
      if (hlsInstance) {
        hlsInstance.destroy();
        hlsInstance = null;
      }
      try {
        await fetch('/api/scan', { method: 'POST' });
      } catch (err) {
        alert('Erro ao disparar varredura: ' + err);
      }
    }

    // Polling de telemetria a cada 400ms
    async function updateStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();

        const badge = document.getElementById('statusBadge');
        const statusText = document.getElementById('statusText');

        if (data.mode === 'play' && data.running) {
          badge.className = 'status-badge';
          statusText.textContent = 'Ao Vivo (Canal ' + data.current_channel + ')';
        } else if (data.mode === 'scan' && data.running) {
          badge.className = 'status-badge scanning';
          statusText.textContent = 'Buscando Canais...';
        } else {
          badge.className = 'status-badge stopped';
          statusText.textContent = 'Parado';
        }

        // Telemetria
        document.getElementById('metricRssi').textContent = data.rssi || '-- dBFS';
        document.getElementById('metricOfdm').textContent = data.ofdm || '--';
        document.getElementById('metricTmcc').textContent = data.tmcc || '--';
        document.getElementById('metricTs').textContent = data.ts_packets || '0';

        // Barra de RSSI (-30 dBFS a 0 dBFS)
        if (data.rssi && data.rssi.includes('dBFS')) {
          const val = parseFloat(data.rssi);
          const pct = Math.max(0, Math.min(100, ((val + 30) / 30) * 100));
          document.getElementById('signalBar').style.width = pct + '%';
        }

        // Logs
        if (data.logs && data.logs.length > 0) {
          const logBox = document.getElementById('logBox');
          logBox.textContent = data.logs.slice(-25).join('\n');
          logBox.scrollTop = logBox.scrollHeight;
        }

        // Atualiza a lista de canais se a varredura terminou
        if (!data.running && badge.dataset.lastMode === 'scan') {
          loadChannels();
        }
        badge.dataset.lastMode = data.mode;

      } catch (e) {
        // Servidor reiniciando ou sem conexao temporaria
      }
    }

    // Iniciar polling e canais
    setInterval(updateStatus, 400);
    loadChannels();
  </script>
</body>
</html>
)rawhtml";

} // namespace isdbt

