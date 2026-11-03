#include "bench_metrics.h"
#include "bench_report.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    int ofdm_mode = 1;
    bool compare_mode = false;
    int repeat_count = 1;
    double segment_sec = 0.0;
    std::string iq_path = "samples/ch18_2min_iq.cu8";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compare") {
            compare_mode = true;
        } else if (arg == "--ofdm" && i + 1 < argc) {
            ofdm_mode = std::stoi(argv[++i]);
        } else if (arg.rfind("--ofdm=", 0) == 0) {
            ofdm_mode = std::stoi(arg.substr(7));
        } else if ((arg == "--repeat" || arg == "-r") && i + 1 < argc) {
            repeat_count = std::stoi(argv[++i]);
        } else if (arg.rfind("--repeat=", 0) == 0) {
            repeat_count = std::stoi(arg.substr(9));
        } else if (arg == "--segment-of" && i + 1 < argc) {
            segment_sec = std::stod(argv[++i]);
        } else if (arg.rfind("--segment-of=", 0) == 0) {
            segment_sec = std::stod(arg.substr(13));
        } else if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            iq_path = argv[++i];
        } else if (arg.rfind("--input=", 0) == 0) {
            iq_path = arg.substr(8);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Uso: bench_sync [opcoes]\n"
                      << "  --ofdm <1|2>         Executa benchmark para o modo especificado (padrao: 1)\n"
                      << "                       (tambem aceita --ofdm=1 ou --ofdm=2)\n"
                      << "  --compare            Compara ofdm 1 vs ofdm 2 com tabela ASCII\n"
                      << "  --repeat <N>         Executa N repeticoes e calcula media (ex: --repeat 5)\n"
                      << "  --segment-of <seg>   Segmenta amostra em janelas temporais de <seg> segundos\n"
                      << "  --input <caminho>    Caminho da amostra IQ (padrao samples/ch18_2min_iq.cu8)\n";
            return 0;
        }
    }

    if (!fs::exists(iq_path)) {
        if (fs::exists("../" + iq_path)) {
            iq_path = "../" + iq_path;
        } else {
            std::cerr << "[ERRO] Arquivo de amostra nao encontrado: " << iq_path << "\n";
            return 1;
        }
    }

    std::cout << "======================================================================\n";
    std::cout << " SUITE DE BENCHMARK OFDM ISDB-T/ISDB-Tb (bench_sync)\n";
    std::cout << " Amostra: " << iq_path << "\n";
    std::cout << " Modo:    " << (compare_mode ? "COMPARE (ofdm=1 vs ofdm=2)" : ("ofdm=" + std::to_string(ofdm_mode))) << "\n";
    std::cout << " Repeticoes: " << repeat_count << "\n";
    std::cout << "======================================================================\n\n";

    if (!compare_mode) {
        std::cout << "[INFO] Executando bench_sync para --ofdm=" << ofdm_mode << "...\n";
        auto res = isdbt::run_benchmark(ofdm_mode, iq_path);
        std::cout << "  wall_time_s:       " << res.wall_time_s << "\n"
                  << "  cpu_user_s:        " << res.cpu_user_s << "\n"
                  << "  cycles_per_symbol: " << res.cycles_per_symbol_mean << " (p95: " << res.cycles_per_symbol_p95 << ")\n"
                  << "  peak_rss_mb:       " << res.peak_rss_mb << "\n"
                  << "  locked_time_ratio: " << res.locked_time_ratio << "\n"
                  << "  valid_ts_packets:  " << res.valid_ts_packets << "\n";
        return 0;
    }

    // Modo comparativo (--compare)
    if (segment_sec > 0.0) {
        // R21: Comparativo segmentado por tempo
        double total_dur_s = 120.0;
        int num_segments = static_cast<int>(total_dur_s / segment_sec);
        std::cout << "[INFO] Executando comparativo em " << num_segments << " janelas de " << segment_sec << "s...\n";

        for (int seg = 0; seg < num_segments; ++seg) {
            double start_s = seg * segment_sec;
            std::ostringstream ss;
            ss << "Janela [" << std::fixed << std::setprecision(1) << start_s << "s - " << (start_s + segment_sec) << "s]";
            auto r1 = isdbt::run_benchmark(1, iq_path, start_s, segment_sec);
            auto r2 = isdbt::run_benchmark(2, iq_path, start_s, segment_sec);
            isdbt::BenchReport::print_comparison(r1, r2, ss.str());
        }
        return 0;
    }

    // Comparativo global (--compare) com suporte a --repeat
    isdbt::BenchRunResult avg1{}, avg2{};
    for (int it = 0; it < repeat_count; ++it) {
        if (repeat_count > 1) {
            std::cout << "[INFO] Executando iteracao " << (it + 1) << " / " << repeat_count << "...\n";
        }
        auto r1 = isdbt::run_benchmark(1, iq_path);
        auto r2 = isdbt::run_benchmark(2, iq_path);

        avg1.wall_time_s += r1.wall_time_s;
        avg1.cpu_user_s += r1.cpu_user_s;
        avg1.cpu_sys_s += r1.cpu_sys_s;
        avg1.cycles_per_symbol_mean += r1.cycles_per_symbol_mean;
        avg1.cycles_per_symbol_p95 += r1.cycles_per_symbol_p95;
        avg1.peak_rss_mb = std::max(avg1.peak_rss_mb, r1.peak_rss_mb);
        avg1.locked_time_ratio += r1.locked_time_ratio;
        avg1.snr_mean_db += r1.snr_mean_db;
        avg1.valid_ts_packets += r1.valid_ts_packets;

        avg2.wall_time_s += r2.wall_time_s;
        avg2.cpu_user_s += r2.cpu_user_s;
        avg2.cpu_sys_s += r2.cpu_sys_s;
        avg2.cycles_per_symbol_mean += r2.cycles_per_symbol_mean;
        avg2.cycles_per_symbol_p95 += r2.cycles_per_symbol_p95;
        avg2.peak_rss_mb = std::max(avg2.peak_rss_mb, r2.peak_rss_mb);
        avg2.locked_time_ratio += r2.locked_time_ratio;
        avg2.snr_mean_db += r2.snr_mean_db;
        avg2.valid_ts_packets += r2.valid_ts_packets;
    }

    avg1.wall_time_s /= repeat_count;
    avg1.cpu_user_s /= repeat_count;
    avg1.cpu_sys_s /= repeat_count;
    avg1.cycles_per_symbol_mean /= repeat_count;
    avg1.cycles_per_symbol_p95 /= repeat_count;
    avg1.locked_time_ratio /= repeat_count;
    avg1.snr_mean_db /= repeat_count;
    avg1.valid_ts_packets /= repeat_count;

    avg2.wall_time_s /= repeat_count;
    avg2.cpu_user_s /= repeat_count;
    avg2.cpu_sys_s /= repeat_count;
    avg2.cycles_per_symbol_mean /= repeat_count;
    avg2.cycles_per_symbol_p95 /= repeat_count;
    avg2.locked_time_ratio /= repeat_count;
    avg2.snr_mean_db /= repeat_count;
    avg2.valid_ts_packets /= repeat_count;

    // Emissao da tabela ASCII oficial
    isdbt::BenchReport::print_comparison(avg1, avg2, "RESULTADO COMPARATIVO OFICIAL");

    // Gravacao do resultado em JSON secundario (R20)
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string json_path = "bench/results/ch18_" + std::to_string(ts) + ".json";
    isdbt::BenchReport::export_json(avg1, avg2, json_path);
    std::cout << "[INFO] Relatorio JSON exportado para " << json_path << "\n";

    auto sanity = isdbt::BenchReport::verify_sanity(avg1, avg2);
    return sanity.passed ? 0 : 1;
}
