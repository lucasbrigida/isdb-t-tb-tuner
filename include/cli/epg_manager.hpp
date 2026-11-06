#pragma once

#include "mpegts/psi_parser.hpp"
#include "cli/channel_manager.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <optional>

namespace isdbt {

struct ChannelEpgEntry {
    int channel_number = 0;
    std::string channel_name;
    uint32_t frequency_hz = 0;
    bool signal = false;
    int64_t last_updated_epoch = 0;
    std::vector<EpgEvent> events;
};

class EpgManager {
public:
    explicit EpgManager(const std::string& path = "epg.json");

    bool load();
    bool save();

    const std::string& path() const { return path_; }
    void set_path(const std::string& p) { path_ = p; }

    // Remove eventos passados cujo horario de encerramento ja passou (end_tp <= now_tp)
    void prune_expired(std::chrono::system_clock::time_point now_tp);

    // Verifica se o canal possui programa valido no ar para o horario atual (start_tp <= now_tp < end_tp)
    bool has_valid_now(int channel_number, std::chrono::system_clock::time_point now_tp,
                       EpgEvent* out_now = nullptr, EpgEvent* out_next = nullptr) const;

    // Recupera canal se existente no cache
    const ChannelEpgEntry* get_channel(int channel_number) const;

    // Recupera ou cria entrada de canal no cache
    ChannelEpgEntry& get_or_create_channel(int channel_number, const std::string& name = "", uint32_t freq_hz = 0);

    // Enriquece os eventos de um canal:
    // Mescla informacoes novas (sinopse, genero, rating, titulo) nos eventos existentes por id/tempo
    // e adiciona novos eventos sem descartar eventos futuros preservados no cache.
    void enrich_channel_events(int channel_number, const std::string& name, uint32_t freq_hz,
                               bool signal, const std::vector<EpgEvent>& new_events,
                               std::chrono::system_clock::time_point now_tp);

    // Retorna todos os canais do cache
    const std::map<int, ChannelEpgEntry>& channels() const { return channels_; }

    // Serializa para JSON (todos os canais ou apenas canal especifico)
    std::string to_json_string(int specific_channel = 0) const;

    // Utilitario para formatar time_point no fuso horario local da maquina
    static std::string format_local_time(const std::chrono::system_clock::time_point& tp, bool include_date = false);

private:
    std::string path_;
    int64_t updated_epoch_ = 0;
    std::map<int, ChannelEpgEntry> channels_;
};

} // namespace isdbt

