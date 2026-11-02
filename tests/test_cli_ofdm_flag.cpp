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

int main() {
    std::cout << "=== Teste de Regras CLI da Flag --ofdm (R1, R2, A1) ===\n";

    std::string bin = find_binary();

    // 1. Testa que valores invalidos para --ofdm no subcomando play retornam erro (R1)
    std::cout << "  [1/4] Testando play --ofdm=3 (valor invalido)...\n";
    std::string cmd1 = bin + " play --ofdm=3 > nul 2>&1";
#ifndef _WIN32
    cmd1 = bin + " play --ofdm=3 > /dev/null 2>&1";
#endif
    int code_invalid = std::system(cmd1.c_str());
    TEST_ASSERT(code_invalid != 0);

    // 2. Testa que --ofdm eh silenciosamente ignorada no subcomando scan (R2, A1)
    std::cout << "  [2/4] Testando scan --ofdm=2 (deve ignorar silenciosamente)...\n";
    std::string cmd2 = bin + " scan --help --ofdm=2 > nul 2>&1";
#ifndef _WIN32
    cmd2 = bin + " scan --help --ofdm=2 > /dev/null 2>&1";
#endif
    int code_scan_ignore = std::system(cmd2.c_str());
    TEST_ASSERT(code_scan_ignore == 0);

    // 3. Testa que --ofdm eh silenciosamente ignorada no subcomando probe (R2, A1)
    std::cout << "  [3/4] Testando probe --ofdm=1 (deve ignorar silenciosamente)...\n";
    std::string cmd3 = bin + " probe --help --ofdm=1 > nul 2>&1";
#ifndef _WIN32
    cmd3 = bin + " probe --help --ofdm=1 > /dev/null 2>&1";
#endif
    int code_probe_ignore = std::system(cmd3.c_str());
    TEST_ASSERT(code_probe_ignore == 0);

    // 4. Testa que --ofdm eh silenciosamente ignorada no comando global --help (R2, A1)
    std::cout << "  [4/4] Testando --help --ofdm=2 (deve ignorar silenciosamente)...\n";
    std::string cmd4 = bin + " --help --ofdm=2 > nul 2>&1";
#ifndef _WIN32
    cmd4 = bin + " --help --ofdm=2 > /dev/null 2>&1";
#endif
    int code_help_ignore = std::system(cmd4.c_str());
    TEST_ASSERT(code_help_ignore == 0);

    std::cout << "[PASS] Todos os requisitos CLI de --ofdm foram validados com sucesso!\n";
    return 0;
}
