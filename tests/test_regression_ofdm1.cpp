#include <iostream>
#include <fstream>
#include <vector>
#include <complex>
#include <cassert>
#include <string>
#include <sstream>
#include <iomanip>
#include "dsp/constants.hpp"
#include "sync/sync_factory.h"
#include "dsp/oneseg_demodulator.hpp"
#include "mpegts/psi_parser.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <openssl/sha.h>
#endif

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

int main(int argc, char** argv) {
    std::cout << "=== Teste de Regressao Bit-a-Bit --ofdm=1 (R14) ===\n";

    std::string iq_path = "samples/ch18_2min_iq.cu8";
    if (argc > 1) {
        iq_path = argv[1];
    }

    std::ifstream iq_file(iq_path, std::ios::binary);
    if (!iq_file.is_open()) {
        std::cerr << "[SKIP] Arquivo de amostra real " << iq_path << " nao encontrado. Pulando teste de regressao.\n";
        return 0;
    }

    // Valida SHA-256 da amostra de entrada (R16)
    std::string expected_sample_sha = "f614eade06dcffcd6f69e10c5c0f6f44ec0d6f67d8e2459036b152f7fe431c50";
    std::ifstream sha_file("samples/ch18_2min_iq.sha256");
    if (sha_file.is_open()) {
        std::string line;
        if (std::getline(sha_file, line)) {
            std::string read_sha = line.substr(0, line.find_first_of(" \t\r\n"));
            if (!read_sha.empty()) expected_sample_sha = read_sha;
        }
    }

    std::string actual_sample_sha = compute_sha256_file(iq_path);
    if (actual_sample_sha != expected_sample_sha) {
        std::cerr << "[SKIP] SHA-256 da amostra divergiu (" << actual_sample_sha << " != " << expected_sample_sha << "). R16 Skip.\n";
        return 0;
    }

    // Le hash esperado do TS
    std::string expected_ts_sha = "";
    std::ifstream ts_sha_f("tests/golden/ch18_baseline_ofdm1.ts.sha256");
    if (ts_sha_f.is_open()) {
        std::string line;
        if (std::getline(ts_sha_f, line)) {
            expected_ts_sha = line.substr(0, line.find_first_of(" \t\r\n"));
        }
    }

    if (expected_ts_sha.empty()) {
        std::cerr << "[FALHA] Golden file ch18_baseline_ofdm1.ts.sha256 nao encontrado!\n";
        return 1;
    }

    std::string run_ts_path = "tests/golden/ch18_current_run_ofdm1.ts";
    std::ofstream ts_out(run_ts_path, std::ios::binary);

    isdbt::SyncParams params;
    params.mode = isdbt::OfdmMode::MODE_1_2K;
    params.gi = isdbt::GuardInterval::GI_1_16;
    params.sample_rate = isdbt::constants::SAMPLE_RATE_HZ;
    params.fft_size = 2048;
    params.gi_size = 128;

    auto synchronizer = isdbt::make_synchronizer(isdbt::OFDMAlgorithm::VanDeBeek, params);
    isdbt::OneSegDemodulator demod(params.mode, params.gi);
    isdbt::PsiParser psi;

    constexpr size_t chunk_samples = 32768;
    constexpr size_t chunk_bytes = chunk_samples * 2;
    std::vector<uint8_t> raw_data(chunk_bytes);
    std::vector<std::complex<float>> iq_buf(chunk_samples);
    std::vector<std::complex<float>> useful_symbols;
    useful_symbols.reserve(chunk_samples);

    uint64_t valid_pkts = 0;

    while (iq_file.read(reinterpret_cast<char*>(raw_data.data()), chunk_bytes)) {
        for (size_t i = 0; i < chunk_samples; ++i) {
            float iv = (static_cast<float>(raw_data[2 * i + 0]) - 127.5f) / 127.5f;
            float qv = (static_cast<float>(raw_data[2 * i + 1]) - 127.5f) / 127.5f;
            iq_buf[i] = std::complex<float>(iv, qv);
        }

        synchronizer->process(iq_buf.data(), chunk_samples, useful_symbols);

        std::vector<std::vector<uint8_t>> decoded_pkts;
        for (size_t s = 0; s + 2048 <= useful_symbols.size(); s += 2048) {
            demod.process_symbol(&useful_symbols[s], decoded_pkts);
        }

        for (const auto& pkt : decoded_pkts) {
            psi.parse_packet(pkt.data());
            ts_out.write(reinterpret_cast<const char*>(pkt.data()), 188);
            valid_pkts++;
        }
    }

    ts_out.close();

    std::string actual_ts_sha = compute_sha256_file(run_ts_path);
    std::cout << "  Pacotes TS Decodificados: " << valid_pkts << "\n";
    std::cout << "  TS Hash Atual:     " << actual_ts_sha << "\n";
    std::cout << "  TS Hash Golden:    " << expected_ts_sha << "\n";

    if (actual_ts_sha == expected_ts_sha) {
        std::cout << "[PASS] Regressao bit-a-bit --ofdm=1 validada com 100% de exatidao!\n";
        return 0;
    } else {
        std::cerr << "[FALHA] Divergencia bit-a-bit detectada entre a execucao atual e o golden file!\n";
        return 1;
    }
}

