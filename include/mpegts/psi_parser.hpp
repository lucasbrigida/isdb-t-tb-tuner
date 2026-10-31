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

    /**
     * @brief Constroi um pacote MPEG-TS PAT (PID 0x0000) de 188 bytes mapeando o servico 1-Seg
     *        descoberto (ex: PMT em PID 0x1FC8), necessario para que players como o VLC
     *        descubram automaticamente o programa em recepcao parcial ISDB-Tb.
     * @param out_ts188 Buffer de saida de 188 bytes.
     * @return true se ja houver um PMT conhecido e o pacote PAT foi gerado.
     */
    bool build_pat_packet(uint8_t* out_ts188);

private:
    void parse_pat(const uint8_t* payload, size_t len);
    void parse_pmt(uint16_t pmt_pid, const uint8_t* payload, size_t len);
    void parse_sdt(const uint8_t* payload, size_t len);

    std::map<uint16_t, ProgramService> services_; // service_id -> ProgramService
    std::map<uint16_t, uint16_t> pmt_pids_;       // pmt_pid -> service_id
    uint8_t pat_cc_ = 0;
};

} // namespace isdbt
