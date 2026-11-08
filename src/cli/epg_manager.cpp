#include "cli/epg_manager.hpp"
#include "third_party/json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cmath>

namespace isdbt {

EpgManager::EpgManager(const std::string& path)
    : path_(path.empty() ? "epg.json" : path) {
}

std::string EpgManager::format_local_time(const std::chrono::system_clock::time_point& tp, bool include_date) {
    if (tp == std::chrono::system_clock::time_point{}) return "--:--";
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char buf[64];
    if (include_date) {
        std::snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d",
                      tm_local.tm_mday, tm_local.tm_mon + 1,
                      tm_local.tm_hour, tm_local.tm_min);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d",
                      tm_local.tm_hour, tm_local.tm_min);
    }
    return std::string(buf);
}

static EpgEvent parse_event_json(const nlohmann::json& item, std::chrono::system_clock::time_point now_tp) {
    EpgEvent evt;
    evt.event_id = item.value("event_id", 0);
    evt.duration_sec = item.value("duration_sec", 0);
    evt.title = item.value("title", "");
    evt.short_desc = item.value("short_desc", "");
    evt.extended_desc = item.value("extended_desc", "");
    evt.genre = item.value("genre", "");
    evt.rating = item.value("rating", "");
    evt.start_time_str = item.value("start_time", "");
    evt.end_time_str = item.value("end_time", "");

    int64_t s_epoch = item.value("start_time_epoch", static_cast<int64_t>(0));
    int64_t e_epoch = item.value("end_time_epoch", static_cast<int64_t>(0));

    if (s_epoch > 0) {
        evt.start_tp = std::chrono::system_clock::time_point(std::chrono::seconds(s_epoch));
    }
    if (e_epoch > 0) {
        evt.end_tp = std::chrono::system_clock::time_point(std::chrono::seconds(e_epoch));
    } else if (s_epoch > 0 && evt.duration_sec > 0) {
        evt.end_tp = evt.start_tp + std::chrono::seconds(evt.duration_sec);
    }

    // Se nao possui epoch (dados legados), tenta inferir com base no horario atual da maquina
    if (evt.start_tp == std::chrono::system_clock::time_point{} && !evt.start_time_str.empty()) {
        int h = 0, m = 0;
        if (std::sscanf(evt.start_time_str.c_str(), "%d:%d", &h, &m) == 2) {
            std::time_t now_t = std::chrono::system_clock::to_time_t(now_tp);
            std::tm now_tm{};
#if defined(_WIN32)
            localtime_s(&now_tm, &now_t);
#else
            localtime_r(&now_t, &now_tm);
#endif
            now_tm.tm_hour = h;
            now_tm.tm_min = m;
            now_tm.tm_sec = 0;
            std::time_t inferred_t = std::mktime(&now_tm);
            if (inferred_t != -1) {
                evt.start_tp = std::chrono::system_clock::from_time_t(inferred_t);
                if (evt.duration_sec > 0) {
                    evt.end_tp = evt.start_tp + std::chrono::seconds(evt.duration_sec);
                }
            }
        }
    }

    if (evt.end_tp == std::chrono::system_clock::time_point{} && !evt.end_time_str.empty() &&
        evt.start_tp != std::chrono::system_clock::time_point{}) {
        int eh = 0, em = 0;
        if (std::sscanf(evt.end_time_str.c_str(), "%d:%d", &eh, &em) == 2) {
            std::time_t st_t = std::chrono::system_clock::to_time_t(evt.start_tp);
            std::tm end_tm{};
#if defined(_WIN32)
            localtime_s(&end_tm, &st_t);
#else
            localtime_r(&st_t, &end_tm);
#endif
            end_tm.tm_hour = eh;
            end_tm.tm_min = em;
            end_tm.tm_sec = 0;
            std::time_t inferred_end = std::mktime(&end_tm);
            if (inferred_end != -1) {
                // Caso cruze a meia-noite
                if (inferred_end < st_t) inferred_end += 86400;
                evt.end_tp = std::chrono::system_clock::from_time_t(inferred_end);
            }
        }
    }

    return evt;
}

bool EpgManager::load() {
    channels_.clear();
    if (!std::filesystem::exists(path_)) {
        return false;
    }

    try {
        std::ifstream file(path_);
        if (!file.is_open()) return false;

        nlohmann::json j;
        file >> j;

        auto now_tp = std::chrono::system_clock::now();
        updated_epoch_ = j.value("updated_epoch", static_cast<int64_t>(0));

        // Formato 1: Array de canais {"channels": [...]}
        if (j.contains("channels") && j["channels"].is_array()) {
            for (const auto& ch_item : j["channels"]) {
                ChannelEpgEntry entry;
                entry.channel_number = ch_item.value("channel", 0);
                entry.channel_name = ch_item.value("channel_name", "");
                entry.frequency_hz = ch_item.value("frequency_hz", 0);
                entry.signal = ch_item.value("signal", true);
                entry.last_updated_epoch = ch_item.value("last_updated_epoch", static_cast<int64_t>(0));

                if (ch_item.contains("events") && ch_item["events"].is_array()) {
                    for (const auto& ev_item : ch_item["events"]) {
                        entry.events.push_back(parse_event_json(ev_item, now_tp));
                    }
                }

                // Compatibilidade com campos now/next legados
                if (ch_item.contains("now") && ch_item["now"].is_object()) {
                    auto now_evt = parse_event_json(ch_item["now"], now_tp);
                    now_evt.is_now = true;
                    if (!now_evt.title.empty() && entry.events.empty()) {
                        entry.events.push_back(now_evt);
                    }
                }
                if (ch_item.contains("next") && ch_item["next"].is_object()) {
                    auto next_evt = parse_event_json(ch_item["next"], now_tp);
                    next_evt.is_next = true;
                    if (!next_evt.title.empty()) {
                        bool exists = false;
                        for (const auto& e : entry.events) {
                            if (e.title == next_evt.title && e.start_time_str == next_evt.start_time_str) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) entry.events.push_back(next_evt);
                    }
                }

                if (entry.channel_number > 0) {
                    channels_[entry.channel_number] = entry;
                }
            }
            return true;
        }

        // Formato 2: Canal unico raiz {"channel": 18, "events": [...]}
        if (j.contains("channel") && j["channel"].is_number_integer()) {
            ChannelEpgEntry entry;
            entry.channel_number = j.value("channel", 0);
            entry.channel_name = j.value("channel_name", "");
            entry.frequency_hz = j.value("frequency_hz", 0);
            entry.signal = true;
            entry.last_updated_epoch = j.value("last_updated_epoch", static_cast<int64_t>(0));

            if (j.contains("events") && j["events"].is_array()) {
                for (const auto& ev_item : j["events"]) {
                    entry.events.push_back(parse_event_json(ev_item, now_tp));
                }
            }
            if (entry.channel_number > 0) {
                channels_[entry.channel_number] = entry;
            }
            return true;
        }

        return false;
    } catch (...) {
        return false;
    }
}

bool EpgManager::save() {
    try {
        nlohmann::json j;
        auto now_tp = std::chrono::system_clock::now();
        int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(now_tp.time_since_epoch()).count();
        j["updated_at"] = format_local_time(now_tp, true);
        j["updated_epoch"] = now_epoch;
        j["total_channels"] = channels_.size();

        nlohmann::json ch_arr = nlohmann::json::array();

        for (const auto& [ch_num, entry] : channels_) {
            nlohmann::json ch_obj;
            ch_obj["channel"] = entry.channel_number;
            ch_obj["channel_name"] = entry.channel_name;
            ch_obj["frequency_hz"] = entry.frequency_hz;
            ch_obj["signal"] = entry.signal;
            ch_obj["last_updated_epoch"] = entry.last_updated_epoch;

            nlohmann::json ev_arr = nlohmann::json::array();
            for (const auto& e : entry.events) {
                nlohmann::json ev_obj;
                ev_obj["event_id"] = e.event_id;
                ev_obj["status"] = (e.is_now ? "AGORA" : (e.is_next ? "SEGUIR" : "AGENDA"));
                ev_obj["title"] = e.title;
                ev_obj["start_time"] = e.start_time_str.empty() ? format_local_time(e.start_tp) : e.start_time_str;
                ev_obj["end_time"] = e.end_time_str.empty() ? format_local_time(e.end_tp) : e.end_time_str;
                ev_obj["duration_sec"] = e.duration_sec;
                ev_obj["genre"] = e.genre;
                ev_obj["rating"] = e.rating;
                ev_obj["short_desc"] = e.short_desc;
                ev_obj["extended_desc"] = e.extended_desc;

                if (e.start_tp != std::chrono::system_clock::time_point{}) {
                    ev_obj["start_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.start_tp.time_since_epoch()).count();
                }
                if (e.end_tp != std::chrono::system_clock::time_point{}) {
                    ev_obj["end_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.end_tp.time_since_epoch()).count();
                }

                ev_arr.push_back(ev_obj);
            }
            ch_obj["events"] = ev_arr;
            ch_arr.push_back(ch_obj);
        }

        j["channels"] = ch_arr;

        std::ofstream file(path_, std::ios::trunc);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void EpgManager::prune_expired(std::chrono::system_clock::time_point now_tp) {
    for (auto& [ch_num, entry] : channels_) {
        // Se ha um evento marcado como is_now, ele serve como ancora oficial
        const EpgEvent* anchor = nullptr;
        for (const auto& e : entry.events) {
            if (e.is_now) {
                anchor = &e;
                break;
            }
        }

        std::vector<EpgEvent> valid_events;
        for (auto& e : entry.events) {
            if (anchor && ((anchor->event_id > 0 && e.event_id == anchor->event_id) ||
                           (e.title == anchor->title && e.start_time_str == anchor->start_time_str))) {
                valid_events.push_back(e);
                continue;
            }
            if (anchor && anchor->start_tp != std::chrono::system_clock::time_point{} &&
                e.end_tp != std::chrono::system_clock::time_point{} && e.end_tp <= anchor->start_tp) {
                continue;
            }
            if (!anchor && e.end_tp != std::chrono::system_clock::time_point{} && e.end_tp <= now_tp) {
                continue;
            }
            valid_events.push_back(e);
        }

        // Se nao havia ancora, recalcula status com base no relogio da maquina
        if (!anchor) {
            bool found_now = false;
            for (auto& e : valid_events) {
                e.is_now = false;
                e.is_next = false;
            }
            for (size_t i = 0; i < valid_events.size(); ++i) {
                auto& e = valid_events[i];
                if (!found_now && e.start_tp <= now_tp && now_tp < e.end_tp) {
                    e.is_now = true;
                    found_now = true;
                    if (i + 1 < valid_events.size()) {
                        valid_events[i + 1].is_next = true;
                    }
                }
            }
            if (!found_now && !valid_events.empty()) {
                valid_events[0].is_next = true;
            }
        }

        entry.events = std::move(valid_events);
    }
}

bool EpgManager::has_valid_now(int channel_number, std::chrono::system_clock::time_point now_tp,
                              EpgEvent* out_now, EpgEvent* out_next) const {
    auto it = channels_.find(channel_number);
    if (it == channels_.end()) return false;

    const auto& entry = it->second;
    const EpgEvent* now_evt = nullptr;
    const EpgEvent* next_evt = nullptr;

    // 1. Prioriza evento marcado com is_now transmitido pela emissora
    for (const auto& e : entry.events) {
        if (e.is_now) {
            now_evt = &e;
            break;
        }
    }

    // 2. Fallback: compara com now_tp se nao houver is_now explicito
    if (!now_evt) {
        for (const auto& e : entry.events) {
            if (e.start_tp <= now_tp && now_tp < e.end_tp) {
                now_evt = &e;
                break;
            }
        }
    }

    if (!now_evt) return false;

    // Busca o proximo programa
    for (const auto& e : entry.events) {
        if (e.is_next) {
            next_evt = &e;
            break;
        }
    }
    if (!next_evt) {
        for (const auto& e : entry.events) {
            if (e.start_tp >= now_evt->end_tp) {
                if (!next_evt || e.start_tp < next_evt->start_tp) {
                    next_evt = &e;
                }
            }
        }
    }

    if (out_now) *out_now = *now_evt;
    if (out_next && next_evt) *out_next = *next_evt;
    return true;
}

const ChannelEpgEntry* EpgManager::get_channel(int channel_number) const {
    auto it = channels_.find(channel_number);
    if (it != channels_.end()) {
        return &it->second;
    }
    return nullptr;
}

ChannelEpgEntry& EpgManager::get_or_create_channel(int channel_number, const std::string& name, uint32_t freq_hz) {
    auto& entry = channels_[channel_number];
    entry.channel_number = channel_number;
    if (!name.empty()) entry.channel_name = name;
    if (freq_hz > 0) entry.frequency_hz = freq_hz;
    return entry;
}

void EpgManager::enrich_channel_events(int channel_number, const std::string& name, uint32_t freq_hz,
                                      bool signal, const std::vector<EpgEvent>& new_events,
                                      std::chrono::system_clock::time_point now_tp) {
    auto& entry = get_or_create_channel(channel_number, name, freq_hz);
    entry.signal = signal;
    entry.last_updated_epoch = std::chrono::duration_cast<std::chrono::seconds>(now_tp.time_since_epoch()).count();

    // Procura por evento ancora transmitido ao vivo pela emissora (Tabela 0x4E Secao 0)
    const EpgEvent* live_anchor = nullptr;
    const EpgEvent* live_next = nullptr;
    for (const auto& ne : new_events) {
        if (ne.is_now && !ne.title.empty()) {
            live_anchor = &ne;
        }
        if (ne.is_next && !ne.title.empty()) {
            live_next = &ne;
        }
    }

    // Mescla / enriquece cada novo evento na lista existente
    for (const auto& new_e : new_events) {
        if (new_e.title.empty()) continue;

        int match_idx = -1;
        for (size_t i = 0; i < entry.events.size(); ++i) {
            auto& old_e = entry.events[i];
            // 1. Identificacao por event_id
            if (new_e.event_id > 0 && old_e.event_id == new_e.event_id) {
                match_idx = static_cast<int>(i);
                break;
            }
            // 2. Identificacao por horario de inicio
            if (new_e.start_tp != std::chrono::system_clock::time_point{} &&
                old_e.start_tp != std::chrono::system_clock::time_point{}) {
                auto diff = std::chrono::duration_cast<std::chrono::seconds>(new_e.start_tp - old_e.start_tp).count();
                if (std::abs(diff) < 120) {
                    match_idx = static_cast<int>(i);
                    break;
                }
            }
            // 3. Identificacao por string de horario identica
            if (!new_e.start_time_str.empty() && old_e.start_time_str == new_e.start_time_str && old_e.title == new_e.title) {
                match_idx = static_cast<int>(i);
                break;
            }
        }

        if (match_idx >= 0) {
            // Enriquece campos existentes
            auto& old_e = entry.events[match_idx];
            if (!new_e.title.empty()) old_e.title = new_e.title;
            if (!new_e.short_desc.empty()) old_e.short_desc = new_e.short_desc;
            if (!new_e.extended_desc.empty()) old_e.extended_desc = new_e.extended_desc;
            if (!new_e.genre.empty()) old_e.genre = new_e.genre;
            if (!new_e.rating.empty()) old_e.rating = new_e.rating;
            if (new_e.duration_sec > 0) old_e.duration_sec = new_e.duration_sec;
            if (new_e.start_tp != std::chrono::system_clock::time_point{}) old_e.start_tp = new_e.start_tp;
            if (new_e.end_tp != std::chrono::system_clock::time_point{}) old_e.end_tp = new_e.end_tp;
            if (!new_e.start_time_str.empty()) old_e.start_time_str = new_e.start_time_str;
            if (!new_e.end_time_str.empty()) old_e.end_time_str = new_e.end_time_str;
            if (new_e.is_now) old_e.is_now = true;
            if (new_e.is_next) old_e.is_next = true;
        } else {
            // Adiciona novo evento a grade
            entry.events.push_back(new_e);
        }
    }

    // Descarta eventos passados
    std::vector<EpgEvent> filtered;
    if (live_anchor != nullptr) {
        // Ancora oficial ao vivo (Table 0x4E Secao 0) e verdade absoluta da emissora
        for (const auto& e : entry.events) {
            bool is_the_anchor = ((live_anchor->event_id > 0 && e.event_id == live_anchor->event_id) ||
                                 (e.title == live_anchor->title && e.start_time_str == live_anchor->start_time_str));
            if (is_the_anchor) {
                filtered.push_back(e);
                continue;
            }
            // Eventos encerrados antes ou no inicio da ancora ja passaram
            if (live_anchor->start_tp != std::chrono::system_clock::time_point{} &&
                e.end_tp != std::chrono::system_clock::time_point{} &&
                e.end_tp <= live_anchor->start_tp) {
                continue;
            }
            if (live_anchor->start_tp != std::chrono::system_clock::time_point{} &&
                e.start_tp != std::chrono::system_clock::time_point{} &&
                e.start_tp < live_anchor->start_tp) {
                continue;
            }
            filtered.push_back(e);
        }
    } else {
        // Fallback: modo offline / sem ancora viva - descarta por relogio do PC
        for (const auto& e : entry.events) {
            if (e.end_tp != std::chrono::system_clock::time_point{} && e.end_tp <= now_tp) {
                continue;
            }
            filtered.push_back(e);
        }
    }
    entry.events = std::move(filtered);

    // Ordena cronologicamente
    std::sort(entry.events.begin(), entry.events.end(), [](const EpgEvent& a, const EpgEvent& b) {
        if (a.start_tp != b.start_tp) {
            return a.start_tp < b.start_tp;
        }
        return a.event_id < b.event_id;
    });

    // Atualiza marcadores is_now e is_next
    for (auto& e : entry.events) {
        e.is_now = false;
        e.is_next = false;
    }

    if (live_anchor != nullptr) {
        int anchor_idx = -1;
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            if ((live_anchor->event_id > 0 && e.event_id == live_anchor->event_id) ||
                (e.title == live_anchor->title && e.start_time_str == live_anchor->start_time_str)) {
                anchor_idx = static_cast<int>(i);
                break;
            }
        }

        if (anchor_idx >= 0) {
            entry.events[anchor_idx].is_now = true;

            int next_idx = -1;
            if (live_next != nullptr) {
                for (size_t i = 0; i < entry.events.size(); ++i) {
                    const auto& e = entry.events[i];
                    if ((live_next->event_id > 0 && e.event_id == live_next->event_id) ||
                        (e.title == live_next->title && e.start_time_str == live_next->start_time_str)) {
                        next_idx = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (next_idx >= 0) {
                entry.events[next_idx].is_next = true;
            } else if (anchor_idx + 1 < static_cast<int>(entry.events.size())) {
                entry.events[anchor_idx + 1].is_next = true;
            }
        }
    } else {
        // Fallback: modo offline / sem ancora viva - busca por relogio do PC
        bool found_now = false;
        for (size_t i = 0; i < entry.events.size(); ++i) {
            auto& e = entry.events[i];
            if (!found_now && e.start_tp <= now_tp && now_tp < e.end_tp) {
                e.is_now = true;
                found_now = true;
                if (i + 1 < entry.events.size()) {
                    entry.events[i + 1].is_next = true;
                }
            }
        }
        if (!found_now && !entry.events.empty()) {
            entry.events[0].is_next = true;
        }
    }
}

std::string EpgManager::to_json_string(int specific_channel) const {
    nlohmann::json j;
    auto now_tp = std::chrono::system_clock::now();
    int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(now_tp.time_since_epoch()).count();

    if (specific_channel > 0) {
        auto it = channels_.find(specific_channel);
        if (it != channels_.end()) {
            const auto& entry = it->second;
            j["channel"] = entry.channel_number;
            j["channel_name"] = entry.channel_name;
            j["frequency_hz"] = entry.frequency_hz;
            j["signal"] = entry.signal;
            j["last_updated_epoch"] = entry.last_updated_epoch;

            nlohmann::json ev_arr = nlohmann::json::array();
            for (const auto& e : entry.events) {
                nlohmann::json ev_obj;
                ev_obj["event_id"] = e.event_id;
                ev_obj["status"] = (e.is_now ? "AGORA" : (e.is_next ? "SEGUIR" : "AGENDA"));
                ev_obj["title"] = e.title;
                ev_obj["start_time"] = e.start_time_str.empty() ? format_local_time(e.start_tp) : e.start_time_str;
                ev_obj["end_time"] = e.end_time_str.empty() ? format_local_time(e.end_tp) : e.end_time_str;
                ev_obj["duration_sec"] = e.duration_sec;
                ev_obj["genre"] = e.genre;
                ev_obj["rating"] = e.rating;
                ev_obj["short_desc"] = e.short_desc;
                ev_obj["extended_desc"] = e.extended_desc;
                if (e.start_tp != std::chrono::system_clock::time_point{}) {
                    ev_obj["start_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.start_tp.time_since_epoch()).count();
                }
                if (e.end_tp != std::chrono::system_clock::time_point{}) {
                    ev_obj["end_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.end_tp.time_since_epoch()).count();
                }
                ev_arr.push_back(ev_obj);
            }
            j["events"] = ev_arr;
            return j.dump(2);
        }
    }

    j["updated_at"] = format_local_time(now_tp, true);
    j["updated_epoch"] = now_epoch;
    j["total_channels"] = channels_.size();

    nlohmann::json ch_arr = nlohmann::json::array();
    for (const auto& [ch_num, entry] : channels_) {
        nlohmann::json ch_obj;
        ch_obj["channel"] = entry.channel_number;
        ch_obj["channel_name"] = entry.channel_name;
        ch_obj["frequency_hz"] = entry.frequency_hz;
        ch_obj["signal"] = entry.signal;
        ch_obj["last_updated_epoch"] = entry.last_updated_epoch;

        nlohmann::json ev_arr = nlohmann::json::array();
        for (const auto& e : entry.events) {
            nlohmann::json ev_obj;
            ev_obj["event_id"] = e.event_id;
            ev_obj["status"] = (e.is_now ? "AGORA" : (e.is_next ? "SEGUIR" : "AGENDA"));
            ev_obj["title"] = e.title;
            ev_obj["start_time"] = e.start_time_str.empty() ? format_local_time(e.start_tp) : e.start_time_str;
            ev_obj["end_time"] = e.end_time_str.empty() ? format_local_time(e.end_tp) : e.end_time_str;
            ev_obj["duration_sec"] = e.duration_sec;
            ev_obj["genre"] = e.genre;
            ev_obj["rating"] = e.rating;
            ev_obj["short_desc"] = e.short_desc;
            ev_obj["extended_desc"] = e.extended_desc;
            if (e.start_tp != std::chrono::system_clock::time_point{}) {
                ev_obj["start_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.start_tp.time_since_epoch()).count();
            }
            if (e.end_tp != std::chrono::system_clock::time_point{}) {
                ev_obj["end_time_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(e.end_tp.time_since_epoch()).count();
            }
            ev_arr.push_back(ev_obj);
        }
        ch_obj["events"] = ev_arr;
        ch_arr.push_back(ch_obj);
    }
    j["channels"] = ch_arr;
    return j.dump(2);
}

} // namespace isdbt

