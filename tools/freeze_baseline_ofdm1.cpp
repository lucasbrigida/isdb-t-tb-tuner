#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <filesystem>
#include "dsp/constants.hpp"
#include "dsp/ofdm_sync.hpp"
#include "dsp/oneseg_demodulator.hpp"
#include "dsp/iq_stream_buffer.hpp"
#include "mpegts/psi_parser.hpp"
#include "third_party/json.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#ifndef CALG_SHA_256
#define CALG_SHA_256 0x0000800c
#endif
#else
#include <openssl/sha.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string compute_sha256_file(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) return "";

#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    char buffer[65536];
    while (f.good()) {
        f.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = f.gcount();
        if (bytes_read > 0) {
            CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(bytes_read), 0);
        }
    }

    BYTE rgbHash[32];
    DWORD cbHash = 32;
    std::ostringstream oss;
    if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        for (DWORD i = 0; i < cbHash; i++) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(rgbHash[i]);
        }
    }
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return oss.str();
#else
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    char buffer[65536];
    while (f.good()) {
        f.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = f.gcount();
        if (bytes_read > 0) {
            SHA256_Update(&ctx, buffer, bytes_read);
        }
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
#endif
}

static const char* state_to_string(isdbt::SyncState st) {
    switch (st) {
        case isdbt::SyncState::Acquisition: return "Acquisition";
        case isdbt::SyncState::Tracking:    return "Tracking";
        case isdbt::SyncState::LostLock:    return "LostLock";
        default:                            return "Unknown";
    }
}

int main(int argc, char** argv) {
    std::string input_iq = "samples/ch24_10s_iq.cu8";
    if (argc > 1) {
        input_iq = argv[1];
    }

    std::ifstream file(input_iq, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ERRO] Nao foi possivel abrir amostra IQ: " << input_iq << "\n";
        return 1;
    }

    fs::create_directories("tests/golden");

    std::string ts_out_path    = "tests/golden/ch24_baseline_ofdm1.ts";
    std::string state_log_path = "tests/golden/ch24_baseline_ofdm1.state.log";
    std::string csv_path       = "tests/golden/ch24_baseline_ofdm1.rssi_snr.csv";
    std::string stats_path     = "tests/golden/ch24_baseline_ofdm1.stats.json";
    std::string sha_path       = "tests/golden/ch24_baseline_ofdm1.ts.sha256";

    std::ofstream ts_out(ts_out_path, std::ios::binary);
    std::ofstream state_log(state_log_path);
    std::ofstream csv_out(csv_path);

    csv_out << "block_idx,rssi_dbfs,locked,peak_corr,snr_db,ffo_hz,fsm_state\n";

    constexpr size_t chunk_samples = 32768;
    constexpr size_t chunk_bytes   = chunk_samples * 2;
    std::vector<uint8_t> raw_data(chunk_bytes);
    std::vector<std::complex<float>> iq_buf(chunk_samples);
    isdbt::IqStreamBuffer stream_iq_buf(chunk_samples * 2);

    constexpr size_t FFT_LEN = 2048;
    size_t GI_LEN = 128;
    size_t SYM_LEN = FFT_LEN + GI_LEN;
    float frac_pos = 0.0f;

    isdbt::OfdmSync sync(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16, isdbt::constants::SAMPLE_RATE_HZ);
    isdbt::OneSegDemodulator demod(isdbt::OfdmMode::MODE_1_2K, isdbt::GuardInterval::GI_1_16);
    isdbt::PsiParser psi;

    bool ofdm_locked = false;
    float tracked_ffo_hz = 0.0f;
    int unlock_counter = 0;

    uint64_t total_blocks = 0;
    uint64_t total_symbols = 0;
    uint64_t locked_symbols = 0;
    uint64_t valid_ts_packets = 0;
    uint64_t sync_loss_count = 0;
    uint64_t resync_count = 0;

    isdbt::SyncState prev_state = isdbt::SyncState::Acquisition;
    state_log << "0," << state_to_string(prev_state) << "\n";

    std::cout << "[freeze_baseline_ofdm1] Processando " << input_iq << "...\n";
    auto start_time = std::chrono::steady_clock::now();

    while (file.read(reinterpret_cast<char*>(raw_data.data()), chunk_bytes)) {
        total_blocks++;
        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

        auto sync_res = sync.find_symbol_boundary(iq_buf.data(), chunk_samples);
        isdbt::SyncState cur_state = sync.sync_state();

        if (cur_state != prev_state) {
            state_log << total_blocks << "," << state_to_string(cur_state) << "\n";
            if (cur_state == isdbt::SyncState::LostLock) {
                sync_loss_count++;
            } else if (prev_state == isdbt::SyncState::LostLock && cur_state == isdbt::SyncState::Tracking) {
                resync_count++;
            }
            prev_state = cur_state;
        }

        csv_out << total_blocks << ","
                << std::fixed << std::setprecision(2) << rssi_dbfs << ","
                << (sync_res.locked ? 1 : 0) << ","
                << std::setprecision(4) << sync_res.peak_correlation << ","
                << std::setprecision(2) << sync_res.snr_est_db << ","
                << std::setprecision(1) << sync_res.ffo_hz << ","
                << state_to_string(cur_state) << "\n";

        std::vector<std::vector<uint8_t>> decoded_pkts;

        if (!ofdm_locked) {
            if (sync_res.locked) {
                ofdm_locked = true;
                unlock_counter = 0;
                GI_LEN = static_cast<size_t>(sync.gi_size());
                SYM_LEN = FFT_LEN + GI_LEN;
                frac_pos = 0.0f;
                tracked_ffo_hz = sync_res.ffo_hz;
                sync.reset_phase();
                sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), tracked_ffo_hz);

                size_t gi_start = (sync_res.symbol_offset >= static_cast<int>(GI_LEN))
                                  ? static_cast<size_t>(sync_res.symbol_offset - static_cast<int>(GI_LEN))
                                  : 0;
                stream_iq_buf.assign(iq_buf.data() + gi_start, iq_buf.size() - gi_start);
            }
        } else {
            if (!sync_res.locked) {
                unlock_counter++;
                if (unlock_counter > 200) {
                    ofdm_locked = false;
                    tracked_ffo_hz = 0.0f;
                    frac_pos = 0.0f;
                    GI_LEN = 128;
                    SYM_LEN = FFT_LEN + GI_LEN;
                    unlock_counter = 0;
                    sync.reset();
                    demod.reset();
                    stream_iq_buf.clear();
                }
            } else {
                unlock_counter = 0;
                const float spacing = isdbt::constants::SAMPLE_RATE_HZ / static_cast<float>(FFT_LEN);
                float residual = sync_res.ffo_hz - tracked_ffo_hz;
                residual -= spacing * std::round(residual / spacing);
                tracked_ffo_hz += 0.1f * residual;
            }

            if (ofdm_locked) {
                sync.correct_frequency_offset(iq_buf.data(), iq_buf.size(), tracked_ffo_hz);
                stream_iq_buf.append(iq_buf.data(), iq_buf.size());
            }
        }

        if (ofdm_locked) {
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

                demod.process_symbol(stream_iq_buf.data_at(sym_d + GI_LEN), decoded_pkts);
                frac_pos += static_cast<float>(SYM_LEN);
                total_symbols++;
                locked_symbols++;
            }

            int consume_i = static_cast<int>(std::floor(frac_pos)) - 8;
            if (consume_i > 0 && static_cast<size_t>(consume_i) <= stream_iq_buf.available()) {
                stream_iq_buf.consume(static_cast<size_t>(consume_i));
                frac_pos -= static_cast<float>(consume_i);
            }

            for (const auto& pkt : decoded_pkts) {
                psi.parse_packet(pkt.data());
                ts_out.write(reinterpret_cast<const char*>(pkt.data()), 188);
                valid_ts_packets++;
            }
        } else {
            total_symbols += (chunk_samples / SYM_LEN);
        }
    }

    ts_out.close();
    state_log.close();
    csv_out.close();

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(end_time - start_time).count();

    std::string ts_hash = compute_sha256_file(ts_out_path);
    std::ofstream sha_f(sha_path);
    sha_f << ts_hash << "  ch18_baseline_ofdm1.ts\n";
    sha_f.close();

    double locked_ratio = total_symbols > 0 ? static_cast<double>(locked_symbols) / total_symbols : 0.0;

    json stats_json;
    stats_json["ofdm_mode"] = 1;
    stats_json["total_blocks"] = total_blocks;
    stats_json["total_symbols"] = total_symbols;
    stats_json["locked_symbols"] = locked_symbols;
    stats_json["locked_time_ratio"] = locked_ratio;
    stats_json["sync_loss_count"] = sync_loss_count;
    stats_json["resync_count"] = resync_count;
    stats_json["valid_ts_packets"] = valid_ts_packets;
    stats_json["corrected_rs_packets"] = demod.corrected_rs_packets();
    stats_json["uncorrectable_rs_packets"] = demod.uncorrectable_rs_packets();
    stats_json["average_evm"] = demod.average_evm();
    stats_json["average_snr_db"] = demod.average_snr_db();
    stats_json["elapsed_seconds"] = elapsed_s;
    stats_json["ts_sha256"] = ts_hash;

    std::ofstream stats_f(stats_path);
    stats_f << stats_json.dump(2) << "\n";
    stats_f.close();

    std::cout << "[freeze_baseline_ofdm1] Concluido com sucesso em " << elapsed_s << "s.\n";
    std::cout << "  TS Hash: " << ts_hash << "\n";
    std::cout << "  Pacotes TS: " << valid_ts_packets << "\n";
    std::cout << "  Simbolos Bloqueados: " << locked_symbols << " / " << total_symbols
              << " (" << (locked_ratio * 100.0) << "%)\n";

    return 0;
}
