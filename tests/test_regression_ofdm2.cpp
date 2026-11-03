#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cassert>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include "dsp/constants.hpp"
#include "sync/sync_factory.h"
#include "sync/robust_grisdbt_sync.h"
#include "dsp/oneseg_demodulator.hpp"
#include "mpegts/psi_parser.hpp"
#include "third_party/json.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
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

static const char* robust_state_to_string(isdbt::RobustSyncState st) {
    switch (st) {
        case isdbt::RobustSyncState::Acquisition: return "Acquisition";
        case isdbt::RobustSyncState::Tracking:    return "Tracking";
        case isdbt::RobustSyncState::LostLock:    return "LostLock";
        case isdbt::RobustSyncState::Resync:      return "Resync";
        default:                                  return "Unknown";
    }
}

int main(int argc, char** argv) {
    std::cout << "=== Teste de Regressao e Golden Files --ofdm=2 (R15) ===\n";

    fs::path source_root = argc > 2 ? fs::path(argv[2]) : fs::current_path();
    fs::path iq_path = source_root / "samples/ch24_10s_iq.cu8";
    if (argc > 1) {
        iq_path = argv[1];
    }

    std::ifstream iq_file(iq_path, std::ios::binary);
    if (!iq_file.is_open()) {
        std::cerr << "[SKIP] Amostra real " << iq_path << " nao encontrada. Pulando teste.\n";
        return 0;
    }

    fs::path golden_dir = source_root / "tests/golden";
    fs::path stats_path = golden_dir / "ch24_baseline_ofdm2.stats.json";
    fs::path sha_path = golden_dir / "ch24_baseline_ofdm2.ts.sha256";
    fs::path run_dir = fs::current_path();
    fs::path run_ts_path = run_dir / "ch24_current_run_ofdm2.ts";
    fs::path state_log_path = run_dir / "ch24_current_run_ofdm2.state.log";
    fs::path csv_path = run_dir / "ch24_current_run_ofdm2.rssi_snr.csv";
    fs::path run_stats_path = run_dir / "ch24_current_run_ofdm2.stats.json";

    bool has_golden = fs::exists(sha_path) && fs::exists(stats_path);
    std::string expected_ts_sha = "";
    if (has_golden) {
        std::ifstream f(sha_path);
        std::string line;
        if (std::getline(f, line)) {
            expected_ts_sha = line.substr(0, line.find_first_of(" \t\r\n"));
        }
    }

    std::ofstream ts_out(run_ts_path, std::ios::binary);
    std::ofstream state_log(state_log_path);
    std::ofstream csv_out(csv_path);
    csv_out << "block_idx,rssi_dbfs,locked,cfo_hz,sco_ppm,robust_state\n";

    isdbt::SyncParams params;
    params.mode = isdbt::OfdmMode::MODE_1_2K;
    params.gi = isdbt::GuardInterval::GI_1_16;
    params.sample_rate = isdbt::constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    isdbt::RobustGRISDBTSync robust_sync(params);
    isdbt::OneSegDemodulator demod(params.mode, params.gi);
    isdbt::PsiParser psi;

    constexpr size_t chunk_samples = 32768;
    constexpr size_t chunk_bytes = chunk_samples * 2;
    std::vector<uint8_t> raw_data(chunk_bytes);
    std::vector<std::complex<float>> iq_buf(chunk_samples);
    std::vector<std::complex<float>> useful_symbols;
    useful_symbols.reserve(chunk_samples);

    uint64_t total_blocks = 0;
    uint64_t total_symbols = 0;
    uint64_t locked_symbols = 0;
    uint64_t valid_pkts = 0;
    uint64_t sync_loss_count = 0;
    uint64_t resync_count = 0;

    isdbt::RobustSyncState prev_state = isdbt::RobustSyncState::Acquisition;
    state_log << "0," << robust_state_to_string(prev_state) << "\n";

    auto start_time = std::chrono::steady_clock::now();

    while (iq_file.read(reinterpret_cast<char*>(raw_data.data()), chunk_bytes)) {
        total_blocks++;
        float pwr_sum = 0.0f;
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
            pwr_sum += (iv * iv + qv * qv);
        }
        float rssi_dbfs = 10.0f * std::log10((pwr_sum / chunk_samples) + 1e-12f);

        robust_sync.process(iq_buf.data(), chunk_samples, useful_symbols);
        isdbt::RobustSyncState cur_state = robust_sync.robust_state();

        if (cur_state != prev_state) {
            state_log << total_blocks << "," << robust_state_to_string(cur_state) << "\n";
            if (cur_state == isdbt::RobustSyncState::LostLock) {
                sync_loss_count++;
            } else if (cur_state == isdbt::RobustSyncState::Resync) {
                resync_count++;
            }
            prev_state = cur_state;
        }

        bool locked = (cur_state == isdbt::RobustSyncState::Tracking);
        csv_out << total_blocks << ","
                << std::fixed << std::setprecision(2) << rssi_dbfs << ","
                << (locked ? 1 : 0) << ","
                << std::setprecision(1) << robust_sync.estimated_cfo_hz() << ","
                << std::setprecision(2) << robust_sync.estimated_sco_ppm() << ","
                << robust_state_to_string(cur_state) << "\n";

        std::vector<std::vector<uint8_t>> decoded_pkts;
        for (size_t s = 0; s + 2048 <= useful_symbols.size(); s += 2048) {
            demod.process_symbol(&useful_symbols[s], decoded_pkts);
            total_symbols++;
            if (locked) locked_symbols++;
        }

        for (const auto& pkt : decoded_pkts) {
            psi.parse_packet(pkt.data());
            ts_out.write(reinterpret_cast<const char*>(pkt.data()), 188);
            valid_pkts++;
        }
    }

    ts_out.close();
    state_log.close();
    csv_out.close();

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(end_time - start_time).count();

    std::string actual_ts_sha = compute_sha256_file(run_ts_path.string());

    double locked_ratio = total_symbols > 0 ? static_cast<double>(locked_symbols) / total_symbols : 0.0;

    json stats_json;
    stats_json["ofdm_mode"] = 2;
    stats_json["total_blocks"] = total_blocks;
    stats_json["total_symbols"] = total_symbols;
    stats_json["locked_symbols"] = locked_symbols;
    stats_json["locked_time_ratio"] = locked_ratio;
    stats_json["sync_loss_count"] = sync_loss_count;
    stats_json["resync_count"] = resync_count;
    stats_json["resync_recovered"] = robust_sync.resync_recovered();
    stats_json["resync_fallback"] = robust_sync.resync_fallback();
    stats_json["state_transitions"] = robust_sync.state_transitions();
    stats_json["valid_ts_packets"] = valid_pkts;
    stats_json["corrected_rs_packets"] = demod.corrected_rs_packets();
    stats_json["uncorrectable_rs_packets"] = demod.uncorrectable_rs_packets();
    stats_json["average_evm"] = demod.average_evm();
    stats_json["average_snr_db"] = demod.average_snr_db();
    stats_json["elapsed_seconds"] = elapsed_s;
    stats_json["ts_sha256"] = actual_ts_sha;

    {
        std::ofstream stats_f(run_stats_path);
        stats_f << stats_json.dump(2) << "\n";
    }



    if (!has_golden) {
        std::cout << "[INFO] Gravando golden baseline de --ofdm=2 em " << golden_dir << "...\n";
        fs::copy_file(run_stats_path, stats_path, fs::copy_options::overwrite_existing);
        std::ofstream sha_f(sha_path);
        sha_f << actual_ts_sha << "  ch24_baseline_ofdm2.ts\n";
        std::cout << "[PASS] Golden baseline de --ofdm=2 criado com sucesso (Hash: " << actual_ts_sha << ").\n";
        return 0;
    }

    json expected_stats;
    {
        std::ifstream stats_f(stats_path);
        expected_stats = json::parse(stats_f);
    }
    bool stats_match = true;
    for (const char* key : {
             "ofdm_mode", "total_blocks", "total_symbols", "locked_symbols",
             "locked_time_ratio",
             "sync_loss_count", "resync_count", "resync_recovered",
             "resync_fallback", "state_transitions", "valid_ts_packets",
             "corrected_rs_packets", "uncorrectable_rs_packets"}) {
        if (!expected_stats.contains(key) || !stats_json.contains(key) ||
            expected_stats[key] != stats_json[key]) {
            std::cerr << "[FALHA] Estatistica golden divergente: " << key << "\n";
            stats_match = false;
        }
    }

    // Leitura das estatisticas do baseline ofdm=1 para comparacao explicita de estados (R15)
    std::cout << "\n=== Comparacao Explicita de Estados Visitados (R15) ===\n";
    fs::path ofdm1_stats_path = golden_dir / "ch24_baseline_ofdm1.stats.json";
    if (fs::exists(ofdm1_stats_path)) {
        std::ifstream ofdm1_f(ofdm1_stats_path);
        json j1 = json::parse(ofdm1_f);
        std::cout << "  Metrica               | ofdm=1     | ofdm=2 (gr-isdbt)\n";
        std::cout << "  ----------------------+------------+------------------\n";
        std::cout << "  locked_time_ratio     | " << std::setw(10) << j1.value("locked_time_ratio", 0.0)
                  << " | " << std::setw(10) << locked_ratio << "\n";
        std::cout << "  sync_loss_count       | " << std::setw(10) << j1.value("sync_loss_count", 0)
                  << " | " << std::setw(10) << sync_loss_count << "\n";
        std::cout << "  resync_count          | " << std::setw(10) << j1.value("resync_count", 0)
                  << " | " << std::setw(10) << resync_count << "\n";
        std::cout << "  valid_ts_packets      | " << std::setw(10) << j1.value("valid_ts_packets", 0)
                  << " | " << std::setw(10) << valid_pkts << "\n";
    }



    if (actual_ts_sha != expected_ts_sha) {
        std::cerr << "[FALHA] Hash TS divergente: " << actual_ts_sha
                  << " != " << expected_ts_sha << "\n";
        return 1;
    }
    if (!stats_match) {
        return 1;
    }

    std::cout << "\n[PASS] Regressao --ofdm=2 validada contra os golden files.\n";
    return 0;
}
