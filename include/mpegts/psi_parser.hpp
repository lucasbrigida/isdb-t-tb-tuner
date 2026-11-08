#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <map>
#include <vector>
#include <optional>
#include <chrono>

namespace isdbt {

struct ProgramService {
    uint16_t service_id = 0;
    std::string service_name;
    uint16_t pmt_pid = 0;
    uint16_t pcr_pid = 0x1FFF;
    uint16_t video_pid = 0;
    uint16_t audio_pid = 0;
};

struct EpgEvent {
    uint16_t event_id = 0;
    uint16_t service_id = 0;
    std::chrono::system_clock::time_point start_tp{};
    std::chrono::system_clock::time_point end_tp{};
    int duration_sec = 0;
    std::string start_time_str; // "HH:MM" ou "DD/MM HH:MM"
    std::string end_time_str;   // "HH:MM"
    std::string title;
    std::string short_desc;
    std::string extended_desc;
    std::string genre;
    std::string rating; // "Livre", "10 anos", "12 anos", etc.
    bool is_now = false;
    bool is_next = false;
};

class PsiParser {
public:
    PsiParser();

    /**
     * @brief Processa um pacote MPEG-TS de 188 bytes e analisa tabelas PSI/SI (PAT, PMT, SDT, EIT).
     * @return true se alguma informacao de canal ou guia (nome, servico ou EPG) foi atualizada/descoberta.
     */
    bool parse_packet(const uint8_t* ts188);

    const std::map<uint16_t, ProgramService>& get_services() const { return services_; }
    std::string get_primary_service_name() const;
    // Returns PCR/video/audio PIDs that need discontinuity markers on a channel switch.
    std::vector<uint16_t> get_discontinuity_pids() const;

    /**
     * @brief Metodos para consulta de eventos de EPG (Present, Following e Schedule).
     */
    const std::map<uint16_t, EpgEvent>& get_present_events() const { return present_events_; }
    const std::map<uint16_t, EpgEvent>& get_following_events() const { return following_events_; }
    const std::vector<EpgEvent>& get_schedule_events() const { return schedule_events_; }

    std::optional<EpgEvent> get_present_event(uint16_t service_id = 0) const;
    std::optional<EpgEvent> get_following_event(uint16_t service_id = 0) const;
    std::vector<EpgEvent> get_all_events(uint16_t service_id = 0) const;

    /**
     * @brief Verifica se um PID corresponde a uma trilha de audio identificada nas PMTs.
     */
    bool is_audio_pid(uint16_t pid) const {
        for (const auto& [id, svc] : services_) {
            if (svc.audio_pid != 0 && svc.audio_pid == pid) return true;
        }
        return false;
    }

    /**
     * @brief Retorna true se ja houver ao menos um PMT conhecido mapeado.
     */
    bool has_pmt() const noexcept { return !pmt_pids_.empty(); }

    /**
     * @brief Constroi um pacote MPEG-TS PAT (PID 0x0000) de 188 bytes mapeando o servico 1-Seg
     *        descoberto (ex: PMT em PID 0x1FC8), necessario para que players como o VLC
     *        descubram automaticamente o programa em recepcao parcial ISDB-Tb.
     * @param out_ts188 Buffer de saida de 188 bytes.
     * @return true se ja houver um PMT conhecido e o pacote PAT foi gerado.
     */
    bool build_pat_packet(uint8_t* out_ts188);

    /**
     * @brief Limpa todos os servicos e mapeamentos acumulados e incrementa o numero de versao
     *        da PAT para forcar o demuxer do player (VLC/MPV) a re-sincronizar os novos streams.
     */
    void reset();

private:
    void parse_pat(const uint8_t* payload, size_t len);
    void parse_pmt(uint16_t pmt_pid, const uint8_t* payload, size_t len);
    void parse_sdt(const uint8_t* payload, size_t len);
    void parse_eit(const uint8_t* payload, size_t len);

    struct SectionAssembler {
        std::vector<uint8_t> buffer;
        size_t expected_length = 0;
        void push_packet(bool pusi, const uint8_t* payload, size_t len, std::vector<std::vector<uint8_t>>& completed);
        void reset() { buffer.clear(); expected_length = 0; }
    };

    std::map<uint16_t, SectionAssembler> assemblers_;

    std::map<uint16_t, ProgramService> services_; // service_id -> ProgramService
    std::map<uint16_t, uint16_t> pmt_pids_;       // pmt_pid -> service_id
    std::map<uint16_t, EpgEvent> present_events_;   // service_id -> evento atual (now)
    std::map<uint16_t, EpgEvent> following_events_; // service_id -> proximo evento (next)
    std::vector<EpgEvent> schedule_events_;         // lista de eventos futuros (schedule)
    uint8_t pat_cc_ = 0;
    uint8_t pat_version_ = 1;
};

} // namespace isdbt
