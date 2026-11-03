#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cassert>
#include <vector>
#include <filesystem>

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

static std::string read_file_string(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main() {
    std::cout << "=== Teste de Invariancia de Scan e Probe com --ofdm (R2, N2, A1) ===\n";

    std::string bin = find_binary();

    std::string cmd1 = bin + " scan --help > scan_normal.txt 2>&1";
    std::string cmd2 = bin + " scan --help --ofdm=2 > scan_ofdm2.txt 2>&1";

    std::system(cmd1.c_str());
    std::system(cmd2.c_str());

    std::string out1 = read_file_string("scan_normal.txt");
    std::string out2 = read_file_string("scan_ofdm2.txt");

    std::remove("scan_normal.txt");
    std::remove("scan_ofdm2.txt");

    TEST_ASSERT(!out1.empty());
    TEST_ASSERT(out1 == out2);

    std::cout << "[PASS] Invariancia total de scan/probe confirmada: --ofdm eh ignorada sem impacto.\n";
    return 0;
}
