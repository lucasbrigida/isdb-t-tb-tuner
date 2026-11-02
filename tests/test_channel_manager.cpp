#include <iostream>
#include <cstdlib>
#include <filesystem>
#include "cli/channel_manager.hpp"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_in_memory_operations() {
    std::cout << "[Test] ChannelManager operacoes em memoria..." << std::endl;

    std::string test_tmp = (std::filesystem::temp_directory_path() / "isdbt_test_nonexistent.json").string();
    std::filesystem::remove(test_tmp);

    isdbt::ChannelManager mgr(test_tmp);
    TEST_ASSERT(mgr.get_channels().empty());

    isdbt::ChannelInfo ch1;
    ch1.channel_number = 14;
    ch1.name = "RBI HD";
    ch1.frequency_hz = 473142857;
    ch1.rssi_dbfs = -12.3f;
    ch1.service_id = 0x07D0;
    mgr.add_or_update(ch1);

    TEST_ASSERT(mgr.get_channels().size() == 1);

    isdbt::ChannelInfo ch2;
    ch2.channel_number = 28;
    ch2.name = "SBT HD";
    ch2.frequency_hz = 557142857;
    ch2.rssi_dbfs = -4.7f;
    ch2.service_id = 0x07E0;
    mgr.add_or_update(ch2);

    TEST_ASSERT(mgr.get_channels().size() == 2);

    // Atualiza ch1 com novo nome e RSSI
    ch1.name = "RBI TV";
    ch1.rssi_dbfs = -11.0f;
    mgr.add_or_update(ch1);

    TEST_ASSERT(mgr.get_channels().size() == 2);
    const auto* found14 = mgr.find_by_number(14);
    TEST_ASSERT(found14 != nullptr);
    TEST_ASSERT(found14->name == "RBI TV");
    TEST_ASSERT(found14->rssi_dbfs == -11.0f);

    // Busca inexistente
    TEST_ASSERT(mgr.find_by_number(99) == nullptr);

    // Busca case-insensitive por nome
    const auto* found_sbt = mgr.find_by_name("sbt");
    TEST_ASSERT(found_sbt != nullptr);
    TEST_ASSERT(found_sbt->channel_number == 28);

    const auto* found_rbi = mgr.find_by_name("rbi");
    TEST_ASSERT(found_rbi != nullptr);
    TEST_ASSERT(found_rbi->channel_number == 14);

    TEST_ASSERT(mgr.find_by_name("globo") == nullptr);
    TEST_ASSERT(mgr.find_by_name("") == nullptr);

    mgr.clear();
    TEST_ASSERT(mgr.get_channels().empty());
}

void test_persistence() {
    std::cout << "[Test] ChannelManager persistencia JSON..." << std::endl;

    std::string test_tmp = (std::filesystem::temp_directory_path() / "isdbt_channels_test.json").string();
    std::filesystem::remove(test_tmp);

    {
        isdbt::ChannelManager mgr(test_tmp);
        isdbt::ChannelInfo ch1{23, "BAND HD", 527142857, -5.4f, 0x07A0, "H.264", "AAC"};
        isdbt::ChannelInfo ch2{24, "CULTURA HD", 533142857, -11.4f, 0x07B0, "H.264", "AAC"};
        mgr.add_or_update(ch1);
        mgr.add_or_update(ch2);
        TEST_ASSERT(mgr.save());
    }

    TEST_ASSERT(std::filesystem::exists(test_tmp));

    {
        isdbt::ChannelManager mgr2(test_tmp);
        TEST_ASSERT(mgr2.get_channels().size() == 2);

        const auto* band = mgr2.find_by_number(23);
        TEST_ASSERT(band != nullptr);
        TEST_ASSERT(band->name == "BAND HD");
        TEST_ASSERT(band->frequency_hz == 527142857);
        TEST_ASSERT(std::abs(band->rssi_dbfs - (-5.4f)) < 0.01f);

        const auto* cultura = mgr2.find_by_name("cultura");
        TEST_ASSERT(cultura != nullptr);
        TEST_ASSERT(cultura->channel_number == 24);
        TEST_ASSERT(cultura->frequency_hz == 533142857);
    }

    std::filesystem::remove(test_tmp);
}

int main() {
    test_in_memory_operations();
    test_persistence();

    std::cout << "\033[1;32m[PASS] Todos os testes de ChannelManager passaram com sucesso!\033[0m\n";
    return 0;
}

