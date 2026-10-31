#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <map>
#include <vector>

namespace isdbt {

struct ProgramService {
    uint16_t service_id = 0;
    std::string service_name;
    uint16_t pmt_pid = 0;
    uint16_t video_pid = 0;
    uint16_t audio_pid = 0;
};

class PsiParser {
public:
    PsiParser();

    /**
     * @brief Processa um pacote MPEG-TS de 188 bytes e analisa tabelas PSI/SI (PAT, PMT, SDT).
     * @return true se alguma informacao de canal (nome ou servico) foi atualizada/descoberta.
     */
    bool parse_packet(const uint8_t* ts188);

    const std::map<uint16_t, ProgramService>& get_services() const { return services_; }
    std::string get_primary_service_name() const;

private:
    void parse_pat(const uint8_t* payload, size_t len);
    void parse_pmt(uint16_t pmt_pid, const uint8_t* payload, size_t len);
    void parse_sdt(const uint8_t* payload, size_t len);

    std::map<uint16_t, ProgramService> services_; // service_id -> ProgramService
    std::map<uint16_t, uint16_t> pmt_pids_;       // pmt_pid -> service_id
};

} // namespace isdbt

