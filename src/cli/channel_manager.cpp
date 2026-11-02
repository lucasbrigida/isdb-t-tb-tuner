#include "cli/channel_manager.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

namespace isdbt {

std::string ChannelManager::get_default_config_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) appdata = std::getenv("USERPROFILE");
    if (!appdata) return "channels.json";
    return std::string(appdata) + "\\isdbtb_rx\\channels.json";
#else
    const char* home = std::getenv("HOME");
    if (!home) return "channels.json";
    return std::string(home) + "/.config/isdbtb_rx/channels.json";
#endif
}

ChannelManager::ChannelManager(const std::string& custom_path)
    : config_path_(custom_path.empty() ? get_default_config_path() : custom_path) {
    load();
}

bool ChannelManager::load() {
    channels_.clear();
    if (!std::filesystem::exists(config_path_)) {
        return false;
    }

    try {
        std::ifstream file(config_path_);
        if (!file.is_open()) return false;

        nlohmann::json j;
        file >> j;

        if (j.contains("channels") && j["channels"].is_array()) {
            for (const auto& item : j["channels"]) {
                ChannelInfo ch;
                ch.channel_number = item.value("channel", 0);
                ch.name = item.value("name", "Desconhecido");
                ch.frequency_hz = item.value("frequency_hz", 0);
                ch.rssi_dbfs = item.value("rssi_dbfs", -99.0f);
                ch.service_id = item.value("service_id", 0);
                ch.video_codec = item.value("video_codec", "H.264");
                ch.audio_codec = item.value("audio_codec", "AAC");
                channels_.push_back(ch);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ChannelManager::save() {
    try {
        std::filesystem::path p(config_path_);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }

        nlohmann::json j;
        j["version"] = "1.0.0";
        j["channels"] = nlohmann::json::array();

        for (const auto& ch : channels_) {
            nlohmann::json item;
            item["channel"] = ch.channel_number;
            item["name"] = ch.name;
            item["frequency_hz"] = ch.frequency_hz;
            item["rssi_dbfs"] = ch.rssi_dbfs;
            item["service_id"] = ch.service_id;
            item["video_codec"] = ch.video_codec;
            item["audio_codec"] = ch.audio_codec;
            j["channels"].push_back(item);
        }

        std::ofstream file(config_path_);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

void ChannelManager::add_or_update(const ChannelInfo& ch) {
    for (auto& existing : channels_) {
        if (existing.channel_number == ch.channel_number || existing.frequency_hz == ch.frequency_hz) {
            existing = ch;
            return;
        }
    }
    channels_.push_back(ch);
}

const ChannelInfo* ChannelManager::find_by_number(int ch_num) const {
    for (const auto& ch : channels_) {
        if (ch.channel_number == ch_num) return &ch;
    }
    return nullptr;
}

const ChannelInfo* ChannelManager::find_by_name(const std::string& name_query) const {
    if (name_query.empty()) return nullptr;

    std::string query_lower = name_query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

    for (const auto& ch : channels_) {
        std::string ch_lower = ch.name;
        std::transform(ch_lower.begin(), ch_lower.end(), ch_lower.begin(), ::tolower);
        if (ch_lower.find(query_lower) != std::string::npos) {
            return &ch;
        }
    }
    return nullptr;
}

} // namespace isdbt
