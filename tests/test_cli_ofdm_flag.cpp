#include <iostream>
#include <cassert>
#include <cstdlib>
#include <string>
#include <sstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

static std::string find_binary() {
    std::vector<std::string> candidates = {
        "isdbtb_rx.exe",
        ".\\isdbtb_rx.exe",
        "build\\isdbtb_rx.exe",
        ".\\build\\isdbtb_rx.exe",
        "./isdbtb_rx.exe",
        "build/isdbtb_rx.exe",
        "./isdbtb_rx",
        "build/isdbtb_rx"
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            return fs::canonical(c).make_preferred().string();
        }
    }
    return "isdbtb_rx";
}

static int run_cmd(const std::string& cmd) {
#ifdef _WIN32
    std::string full_cmd = cmd + " > nul 2>&1";
#else
    std::string full_cmd = cmd + " > /dev/null 2>&1";
#endif
    return std::system(full_cmd.c_str());
}

int main() {
    std::cout << "=== Teste de Regras CLI da Flag --ofdm (Espaco e Sinal de Igual) ===\n";

    std::string bin = find_binary();

    // 1. Testa aceitacao de --ofdm 1 e --ofdm 2 no subcomando play (sintaxe separada por espaco)
    std::cout << "  [1/7] Testando play --help --ofdm 1 e --ofdm 2 (espaco)...\n";
    TEST_ASSERT(run_cmd(bin + " play --help --ofdm 1") == 0);
    TEST_ASSERT(run_cmd(bin + " play --help --ofdm 2") == 0);

    // 2. Testa aceitacao de --ofdm=1 e --ofdm=2 no subcomando play (sintaxe com sinal de igual)
    std::cout << "  [2/7] Testando play --help --ofdm=1 e --ofdm=2 (sinal de igual)...\n";
    TEST_ASSERT(run_cmd(bin + " play --help --ofdm=1") == 0);
    TEST_ASSERT(run_cmd(bin + " play --help --ofdm=2") == 0);

    // 3. Testa rejeicao de valores invalidos com espaco e sinal de igual no play
    std::cout << "  [3/7] Testando play --ofdm 3 e play --ofdm=3 (valores invalidos)...\n";
    TEST_ASSERT(run_cmd(bin + " play --ofdm 3") != 0);
    TEST_ASSERT(run_cmd(bin + " play --ofdm=3") != 0);

    // 4. Testa que --ofdm eh silenciosamente ignorada no scan (espaco e igual)
    std::cout << "  [4/7] Testando scan --help --ofdm 2 e --ofdm=2 (deve ignorar)...\n";
    TEST_ASSERT(run_cmd(bin + " scan --help --ofdm 2") == 0);
    TEST_ASSERT(run_cmd(bin + " scan --help --ofdm=2") == 0);

    // 5. Testa que --ofdm eh silenciosamente ignorada no probe (espaco e igual)
    std::cout << "  [5/7] Testando probe --help --ofdm 1 e --ofdm=1 (deve ignorar)...\n";
    TEST_ASSERT(run_cmd(bin + " probe --help --ofdm 1") == 0);
    TEST_ASSERT(run_cmd(bin + " probe --help --ofdm=1") == 0);

    // 6. Testa que --ofdm eh silenciosamente ignorada no comando global --help (espaco e igual)
    std::cout << "  [6/7] Testando --help --ofdm 2 e --help --ofdm=2 (deve ignorar)...\n";
    TEST_ASSERT(run_cmd(bin + " --help --ofdm 2") == 0);
    TEST_ASSERT(run_cmd(bin + " --help --ofdm=2") == 0);

    // 7. Testa bench_sync com --ofdm 2 e --ofdm=2
    std::cout << "  [7/7] Testando bench_sync --help --ofdm 2...\n";
    std::string bench_bin = "bench_sync";
    std::vector<std::string> bench_candidates = {
        "bench_sync.exe",
        ".\\bench_sync.exe",
        "build\\bench_sync.exe",
        ".\\build\\bench_sync.exe",
        "./bench_sync.exe",
        "build/bench_sync.exe",
        "./bench_sync",
        "build/bench_sync"
    };
    for (const auto& c : bench_candidates) {
        if (fs::exists(c)) {
            bench_bin = fs::canonical(c).make_preferred().string();
            break;
        }
    }
    TEST_ASSERT(run_cmd(bench_bin + " --help --ofdm 2") == 0);

    std::cout << "[PASS] Todos os requisitos CLI de --ofdm (espaco e igual) foram validados com sucesso!\n";
    return 0;
}
