#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "json.hpp"

namespace isdbt {

struct ChannelInfo {
    int channel_number = 0;       // Ex: 14, 28, 30
    std::string name;             // Ex: "TV GLOBO", "RECORD", "SBT"
    uint32_t frequency_hz = 0;    // Ex: 569142857
    float rssi_dbfs = 0.0f;       // Ex: -1.95
    uint16_t service_id = 0;      // Service ID do 1-Seg
    std::string video_codec = "H.264";
    std::string audio_codec = "AAC";
};

class ChannelManager {
public:
    explicit ChannelManager(const std::string& custom_path = "");

    bool load();
    bool save();

    void clear() { channels_.clear(); }
    void add_or_update(const ChannelInfo& ch);
    const std::vector<ChannelInfo>& get_channels() const { return channels_; }

    const ChannelInfo* find_by_number(int ch_num) const;
    const ChannelInfo* find_by_name(const std::string& name_query) const;

    static std::string get_default_config_path();

private:
    std::string config_path_;
    std::vector<ChannelInfo> channels_;
};

} // namespace isdbt
