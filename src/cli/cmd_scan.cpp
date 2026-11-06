#include "cli/commands.hpp"
#include "cli/channel_manager.hpp"
#include "radio/frequency_plan.hpp"
#include "radio/rtlsdr_device.hpp"
#include "platform/platform_utils.hpp"
#include "dsp/constants.hpp"
#include "dsp/spsc_capture_queue.hpp"
#include "dsp/iq_stream_buffer.hpp"
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "mpegts/psi_parser.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <complex>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <rtl-sdr.h>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace isdbt {

struct CandidateChannel {
    int ch = 0;
    uint32_t freq_hz = 0;
    float rssi_dbfs = 0.0f;
    float peak_corr = 0.0f;
    GuardInterval detected_gi = GuardInterval::GI_1_16;
    std::string name;
    uint16_t service_id = 0;
};

static void reset_scan_reception(
    OfdmSync& sync,
    OneSegDemodulator& demod,
    IqStreamBuffer& stream_iq_buf,
    bool& ofdm_locked,
    float& tracked_ffo_hz,
    float& frac_pos,
    std::size_t& gi_len,
    std::size_t& sym_len,
    int& unlock_counter) {

    stream_iq_buf.clear();
    frac_pos = 0.0f;
    ofdm_locked = false;
    tracked_ffo_hz = 0.0f;
    unlock_counter = 0;
    gi_len = 0;
    sym_len = 0;

    sync.reset();
    demod.reset();
}

static void update_scan_progress(int done, int total, int ch, uint32_t freq_hz, const std::string& status) {
    float frac = (total > 0) ? (static_cast<float>(done) / static_cast<float>(total)) : 0.0f;
    int percent = static_cast<int>(frac * 100.0f);
    if (percent > 100) percent = 100;

    const int bar_width = 24;
    int filled = static_cast<int>(frac * bar_width);
    if (filled > bar_width) filled = bar_width;

    std::string bar = "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) bar += "=";
        else if (i == filled) bar += ">";
        else bar += " ";
    }
    bar += "]";

    std::stringstream ss;
    ss << "Progresso: " << bar << " " << std::setw(3) << percent << "% | Canal " << ch
       << " (" << std::fixed << std::setprecision(1) << (freq_hz / 1e6) << " MHz) - " << status;

    std::string line = ss.str();
    if (line.length() < 95) {
        line += std::string(95 - line.length(), ' ');
    }
    std::cout << "\r" << line << std::flush;
}

int cmd_scan(int start_ch, int end_ch, int dev_idx, int sdt_timeout_s) {
    print_cli_header();
    constexpr uint32_t sample_rate = constants::SAMPLE_RATE;

    std::cout << "========================================================\n";
    std::cout << "  VARREDURA PROFUNDA DE FREQUENCIAS ISDB-T / ISDB-Tb    \n";
    std::cout << "========================================================\n";
    std::cout << "Faixa: Canais UHF " << start_ch << " a " << end_ch << " | Fs = " << sample_rate << " S/s\n\n";

    rtlsdr_dev_t* dev = nullptr;
    if (rtlsdr_open(&dev, static_cast<uint32_t>(dev_idx)) != 0) {
        std::cerr << "[ERRO] Falha ao abrir dispositivo RTL-SDR #" << dev_idx << "\n";
        return 1;
    }

    rtlsdr_set_sample_rate(dev, sample_rate);
    // Usa ganho fixo calibrado (28.0 dB) sem AGC para medir com precisao o RF real
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, 280);
    rtlsdr_set_agc_mode(dev, 0);

    ChannelManager mgr;
    mgr.clear();
    OfdmSync sync(OfdmMode::MODE_1_2K, GuardInterval::GI_1_16, static_cast<float>(sample_rate));

    size_t chunk_samples = 65536;
    std::vector<uint8_t> raw_buf(chunk_samples * 2);
    std::vector<std::complex<float>> iq_buf(chunk_samples);

    int total_channels = (end_ch - start_ch + 1);
    int scanned_count = 0;
    int channels_found = 0;

    // Tabela unica
    std::cout << " Canal |  Frequencia  |    Forca do Sinal    |  Potencia  | Emissora / Servico\n";
    std::cout << "-------+--------------+----------------------+------------+--------------------\n";

    for (int ch = start_ch; ch <= end_ch && g_running; ++ch) {
        sync.reset();
        uint32_t freq_hz = get_channel_freq(ch);
        update_scan_progress(scanned_count, total_channels, ch, freq_hz, "Analisando RF...");

        safe_set_freq(dev, freq_hz);

        int n_read = 0;
        rtlsdr_read_sync(dev, raw_buf.data(), static_cast<int>(raw_buf.size()), &n_read);

        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_buf[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_buf[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

        auto sync_res = sync.find_symbol_boundary(iq_buf.data(), iq_buf.size());

        // Criterio calibrado: Potencia de RF (>= -26.0 dBFS) e correlacao de CP (>= 0.35)
        bool is_locked = (rssi_dbfs >= -26.0f) && (sync_res.peak_correlation >= 0.35f);

        if (is_locked) {
            update_scan_progress(scanned_count, total_channels, ch, freq_hz,
                                 "Sinal detectado! Lendo SDT (max " + std::to_string(sdt_timeout_s) + ".0s)...");

            CandidateChannel cand;
            cand.ch = ch;
            cand.freq_hz = freq_hz;
            cand.rssi_dbfs = rssi_dbfs;
            cand.peak_corr = sync_res.peak_correlation;
            cand.detected_gi = sync_res.detected_gi;
            cand.name = "Canal " + std::to_string(ch);

            const size_t step_chunk_samples = 32768;
            const size_t step_chunk_bytes = step_chunk_samples * 2;
            auto raw_queue = std::make_unique<SpscCaptureQueue<64>>();
            std::atomic<bool> pass2_worker_running{true};

            std::thread reader_thread([&]() {
#ifdef _WIN32
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
                std::uint64_t sequence = 0;
                bool pending_discontinuity = false;

                while (pass2_worker_running.load(std::memory_order_relaxed) && g_running) {
                    SpscCaptureQueue<64>::WriteView view;
                    if (!raw_queue->try_acquire_write(view)) {
                        raw_queue->record_drop();
                        ++sequence;
                        pending_discontinuity = true;
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        continue;
                    }

                    auto* block = view.block;
                    block->sequence = sequence++;
                    block->size = 0;
                    block->discontinuity_before = pending_discontinuity;

                    int n_r = 0;
                    int r = rtlsdr_read_sync(dev, block->raw.data(), static_cast<int>(step_chunk_bytes), &n_r);
                    if (r < 0 || n_r != static_cast<int>(step_chunk_bytes)) {
                        block->discontinuity_before = true;
                        block->size = 0;
                        pending_discontinuity = true;
                        raw_queue->publish_write(view);
                        continue;
                    }

                    block->size = static_cast<size_t>(n_r);
                    raw_queue->publish_write(view);
                    pending_discontinuity = false;
                }
            });

            constexpr size_t FFT_LEN = 2048;
            size_t GI_LEN = 256;
            if (cand.detected_gi == GuardInterval::GI_1_4) GI_LEN = 512;
            else if (cand.detected_gi == GuardInterval::GI_1_8) GI_LEN = 256;
            else if (cand.detected_gi == GuardInterval::GI_1_16) GI_LEN = 128;
            else if (cand.detected_gi == GuardInterval::GI_1_32) GI_LEN = 64;
            size_t SYM_LEN = FFT_LEN + GI_LEN;

            OfdmSync scan_sync(OfdmMode::MODE_1_2K, cand.detected_gi, static_cast<float>(sample_rate));
            OneSegDemodulator scan_demod(OfdmMode::MODE_1_2K, cand.detected_gi);
            PsiParser scan_psi;

            std::vector<std::complex<float>> cand_iq_buf(step_chunk_samples);
            IqStreamBuffer stream_iq_buf(step_chunk_samples * 2);

            float frac_pos = 0.0f;
            float tracked_ffo_hz = 0.0f;
            bool initial_symbol_found = false;
            std::string detected_name;
            uint16_t detected_service_id = 0;
            int unlock_counter = 0;
            std::uint64_t expected_sequence = 0;
            bool have_expected_sequence = false;

            auto start_time = std::chrono::steady_clock::now();
            while (g_running) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                if (elapsed_ms >= static_cast<int64_t>(sdt_timeout_s) * 1000) break; // Timeout configuravel (padrao 30s)

                SpscCaptureQueue<64>::ReadView view;
                if (!raw_queue->try_acquire_read(view)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    continue;
                }

                const auto& block = *view.block;
                bool discontinuity = block.discontinuity_before;
                if (have_expected_sequence && block.sequence != expected_sequence) {
                    discontinuity = true;
                }

                expected_sequence = block.sequence + 1;
                have_expected_sequence = true;

                if (discontinuity) {
                    reset_scan_reception(scan_sync, scan_demod, stream_iq_buf,
                                         initial_symbol_found, tracked_ffo_hz, frac_pos,
                                         GI_LEN, SYM_LEN, unlock_counter);
                }

                if (block.size == step_chunk_bytes) {
                    const uint8_t* raw_data = block.raw.data();
                    for (size_t i = 0; i < step_chunk_samples; ++i) {
                        float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
                        float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
                        cand_iq_buf[i] = std::complex<float>(iv, qv);
                    }

                    auto s_res = scan_sync.find_symbol_boundary(cand_iq_buf.data(), step_chunk_samples);
                    if (!initial_symbol_found) {
                        if (s_res.locked) {
                            initial_symbol_found = true;
                            unlock_counter = 0;
                            GI_LEN = static_cast<size_t>(scan_sync.gi_size());
                            SYM_LEN = FFT_LEN + GI_LEN;
                            frac_pos = 0.0f;
                            tracked_ffo_hz = s_res.ffo_hz;
                            scan_sync.reset_phase();
                            scan_sync.correct_frequency_offset(cand_iq_buf.data(), cand_iq_buf.size(), tracked_ffo_hz);

                            size_t gi_start = (s_res.symbol_offset >= static_cast<int>(GI_LEN))
                                              ? static_cast<size_t>(s_res.symbol_offset - static_cast<int>(GI_LEN))
                                              : 0;
                            stream_iq_buf.assign(cand_iq_buf.data() + gi_start, cand_iq_buf.size() - gi_start);
                        }
                    } else {
                        if (!s_res.locked) {
                            unlock_counter++;
                            if (unlock_counter > 200) {
                                reset_scan_reception(scan_sync, scan_demod, stream_iq_buf,
                                                     initial_symbol_found, tracked_ffo_hz, frac_pos,
                                                     GI_LEN, SYM_LEN, unlock_counter);
                            }
                        } else {
                            unlock_counter = 0;
                            const float spacing = static_cast<float>(sample_rate) / static_cast<float>(FFT_LEN);
                            float residual = s_res.ffo_hz - tracked_ffo_hz;
                            residual -= spacing * std::round(residual / spacing);
                            tracked_ffo_hz += 0.1f * residual;
                        }

                        if (initial_symbol_found) {
                            scan_sync.correct_frequency_offset(cand_iq_buf.data(), step_chunk_samples, tracked_ffo_hz);
                            stream_iq_buf.append(cand_iq_buf.data(), cand_iq_buf.size());
                        }
                    }

                    if (initial_symbol_found) {
                        std::vector<std::vector<uint8_t>> scan_pkts;
                        while (static_cast<size_t>(std::lround(frac_pos)) + SYM_LEN + 8 <= stream_iq_buf.available()) {
                            int base_pos = static_cast<int>(std::lround(frac_pos));
                            int max_neg = (base_pos >= 4) ? 4 : base_pos;
                            int best_sh = 0;
                            float best_mag = -1.0f;
                            for (int sh = -max_neg; sh <= 4; ++sh) {
                                std::complex<float> g(0.0f, 0.0f);
                                for (size_t m = 0; m < GI_LEN; ++m) {
                                    g += stream_iq_buf[base_pos + sh + m] * std::conj(stream_iq_buf[base_pos + sh + m + FFT_LEN]);
                                }
                                float mag = std::norm(g);
                                if (mag > best_mag) { best_mag = mag; best_sh = sh; }
                            }
                            frac_pos += 0.25f * static_cast<float>(best_sh);
                            int sym_d = std::max(0, static_cast<int>(std::lround(frac_pos)));
                            if (static_cast<size_t>(sym_d) + SYM_LEN > stream_iq_buf.available()) break;

                            scan_demod.process_symbol(stream_iq_buf.data_at(sym_d + GI_LEN), scan_pkts);
                            frac_pos += static_cast<float>(SYM_LEN);
                        }

                        int consume_i = static_cast<int>(std::floor(frac_pos)) - 8;
                        if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf.available()) {
                            stream_iq_buf.consume(static_cast<size_t>(consume_i));
                            frac_pos -= static_cast<float>(consume_i);
                        }

                        for (const auto& pkt : scan_pkts) {
                            scan_psi.parse_packet(pkt.data());
                        }

                        std::string s_name = scan_psi.get_primary_service_name();
                        if (!s_name.empty()) {
                            detected_name = s_name;
                            if (!scan_psi.get_services().empty()) {
                                detected_service_id = scan_psi.get_services().begin()->first;
                            }
                            raw_queue->release_read(view);
                            break;
                        }
                    }
                }
                raw_queue->release_read(view);
            }

            pass2_worker_running.store(false, std::memory_order_release);
            if (reader_thread.joinable()) {
                reader_thread.join();
            }
            rtlsdr_reset_buffer(dev);

            if (!detected_name.empty()) {
                cand.name = detected_name;
                cand.service_id = detected_service_id;
            }

            ChannelInfo info;
            info.channel_number = cand.ch;
            info.frequency_hz = cand.freq_hz;
            info.rssi_dbfs = cand.rssi_dbfs;
            info.name = cand.name;
            info.service_id = cand.service_id;
            mgr.add_or_update(info);

            // Limpa a linha de progresso e insere a linha definitiva na tabela
            std::cout << "\r" << std::string(95, ' ') << "\r";

            std::stringstream freq_ss;
            freq_ss << std::fixed << std::setprecision(3) << (cand.freq_hz / 1e6) << " MHz";

            std::stringstream pwr_ss;
            pwr_ss << std::fixed << std::setprecision(1) << cand.rssi_dbfs << " dBFS";

            auto rating = get_signal_rating(cand.rssi_dbfs);
            std::string raw_signal = rating.bar + " " + rating.text;

            std::cout << " " << std::setw(5) << cand.ch << " | "
                      << std::setw(12) << freq_ss.str() << " | "
                      << pad_string(raw_signal, 20) << " | "
                      << std::setw(10) << pwr_ss.str() << " | "
                      << cand.name << "\n";

            channels_found++;
        }

        scanned_count++;
    }

    // Limpa a linha de progresso residual
    std::cout << "\r" << std::string(95, ' ') << "\r";

    rtlsdr_close(dev);
    mgr.save();

    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << "Varredura concluida: " << channels_found << " canais detectados e salvos!\n";
    std::cout << "Arquivo de configuracao: " << ChannelManager::get_default_config_path() << "\n\n";

    return 0;
}

} // namespace isdbt
