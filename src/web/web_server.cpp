#include "web/web_server.hpp"
#include "web/web_ui.hpp"
#include "json.hpp"

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
    #include <shellapi.h>
#endif

namespace isdbt {

WebServer::WebServer(ProcessManager& proc_mgr, uint16_t port, const std::string& bind_ip, uint16_t udp_stream_port)
    : proc_mgr_(proc_mgr), port_(port), bind_ip_(bind_ip), udp_stream_port_(udp_stream_port) {
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (is_running_) return true;

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[WebServer] Falha ao inicializar WinSock2\n";
        return false;
    }
#endif

    // 1. Criar socket TCP HTTP
    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCK) {
        std::cerr << "[WebServer] Falha ao criar socket TCP\n";
        return false;
    }

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (bind_ip_ == "0.0.0.0" || bind_ip_.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, bind_ip_.c_str(), &server_addr.sin_addr);
    }

    if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        std::cerr << "[WebServer] Falha ao fazer bind na porta TCP " << port_ << "\n";
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return false;
    }

    if (listen(listen_sock_, 16) != 0) {
        std::cerr << "[WebServer] Falha no listen na porta TCP " << port_ << "\n";
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return false;
    }

    // 2. Criar socket UDP para escutar o stream do isdbtb_rx play (127.0.0.1:1234)
    udp_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock_ != INVALID_SOCK) {
        setsockopt(udp_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in udp_addr{};
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_port = htons(udp_stream_port_);
        udp_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(udp_sock_, reinterpret_cast<sockaddr*>(&udp_addr), sizeof(udp_addr)) != 0) {
            std::cerr << "[WebServer] Aviso: Nao foi possivel abrir socket UDP na porta " << udp_stream_port_ << "\n";
        }
    }

    is_running_ = true;
    server_thread_ = std::thread(&WebServer::accept_loop, this);
    if (udp_sock_ != INVALID_SOCK) {
        udp_thread_ = std::thread(&WebServer::udp_relay_loop, this);
    }

    std::cout << "[WebServer] Servidor Web ativo em http://" << (bind_ip_ == "0.0.0.0" ? "localhost" : bind_ip_) << ":" << port_ << "/\n";
    return true;
}

void WebServer::stop() {
    if (!is_running_.exchange(false)) return;

    if (listen_sock_ != INVALID_SOCK) {
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
    }
    if (udp_sock_ != INVALID_SOCK) {
        CLOSE_SOCK(udp_sock_);
        udp_sock_ = INVALID_SOCK;
    }

    {
        std::lock_guard<std::mutex> lock(stream_clients_mtx_);
        for (socket_t sock : stream_clients_) {
            CLOSE_SOCK(sock);
        }
        stream_clients_.clear();
    }

    if (server_thread_.joinable()) server_thread_.join();
    if (udp_thread_.joinable()) udp_thread_.join();

#ifdef _WIN32
    WSACleanup();
#endif
}

void WebServer::udp_relay_loop() {
    std::vector<uint8_t> buffer(65536);

    while (is_running_.load()) {
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        int n = recvfrom(udp_sock_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                         reinterpret_cast<sockaddr*>(&sender), &sender_len);

        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Alimenta o FFmpeg em tempo real via pipe stdin para geracao dos fragmentos HLS (.m3u8 e .ts)
        proc_mgr_.write_ffmpeg_input(buffer.data(), static_cast<size_t>(n));

        // Analisa os pacotes PSI/SI (PAT, PMT, SDT) para descobrir e rastrear os PIDs de video e audio
        for (int i = 0; i + 188 <= n; i += 188) {
            if (buffer[i] == 0x47) {
                psi_parser_.parse_packet(&buffer.data()[i]);
            }
        }

        std::lock_guard<std::mutex> lock(stream_clients_mtx_);
        if (stream_clients_.empty()) continue;

        // Cria uma copia dos pacotes para envio HTTP via /stream.ts.
        // Pacotes com o PID de audio (ex: AAC LOAS/LATM do ISDB-Tb) sao substituidos por
        // pacotes nulos (Null Packets PID 0x1FFF). Isso preserva a taxa de dados sem enviar
        // audio incompativel que causa 'frameLengthType = 4' no demuxer do navegador.
        std::vector<uint8_t> http_buffer(buffer.begin(), buffer.begin() + n);
        for (int i = 0; i + 188 <= n; i += 188) {
            if (http_buffer[i] != 0x47) continue;
            uint16_t pid = ((static_cast<uint16_t>(http_buffer[i + 1] & 0x1F)) << 8) | http_buffer[i + 2];
            if (psi_parser_.is_audio_pid(pid)) {
                // TS Null packet
                http_buffer[i + 1] = 0x1F;
                http_buffer[i + 2] = 0xFF;
                http_buffer[i + 3] = 0x10;
                std::memset(&http_buffer[i + 4], 0xFF, 184);
            }
        }

        std::vector<socket_t> to_remove;
        for (socket_t client : stream_clients_) {
            int sent = send(client, reinterpret_cast<const char*>(http_buffer.data()), n, 0);
            if (sent <= 0) {
                to_remove.push_back(client);
            }
        }

        for (socket_t dead : to_remove) {
            CLOSE_SOCK(dead);
            stream_clients_.erase(dead);
        }
    }
}

void WebServer::accept_loop() {
    while (is_running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t client_sock = accept(listen_sock_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_sock == INVALID_SOCK) {
            if (!is_running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::thread([this, client_sock]() {
            this->handle_client(client_sock);
        }).detach();
    }
}

static std::string get_query_param(const std::string& query, const std::string& param) {
    size_t pos = query.find(param + "=");
    if (pos == std::string::npos) return "";
    size_t start = pos + param.length() + 1;
    size_t end = query.find('&', start);
    if (end == std::string::npos) end = query.length();
    return query.substr(start, end - start);
}

void WebServer::handle_client(socket_t client_sock) {
    char req_buf[4096];
    int n = recv(client_sock, req_buf, sizeof(req_buf) - 1, 0);
    if (n <= 0) {
        CLOSE_SOCK(client_sock);
        return;
    }
    req_buf[n] = '\0';

    std::string request(req_buf);

    // Se o cabecalho tiver Content-Length e o corpo ainda nao chegou por completo, continua lendo
    size_t cl_pos = request.find("Content-Length: ");
    if (cl_pos == std::string::npos) cl_pos = request.find("content-length: ");
    if (cl_pos != std::string::npos) {
        size_t cl_end = request.find("\r\n", cl_pos);
        if (cl_end != std::string::npos) {
            try {
                int content_len = std::stoi(request.substr(cl_pos + 16, cl_end - (cl_pos + 16)));
                size_t b_pos = request.find("\r\n\r\n");
                if (b_pos != std::string::npos) {
                    size_t body_received = request.size() - (b_pos + 4);
                    while (body_received < static_cast<size_t>(content_len)) {
                        char chunk[1024];
                        int chunk_n = recv(client_sock, chunk, sizeof(chunk) - 1, 0);
                        if (chunk_n <= 0) break;
                        request.append(chunk, chunk_n);
                        body_received += chunk_n;
                    }
                }
            } catch (...) {}
        }
    }

    std::istringstream req_stream(request);
    std::string method, path, proto;
    req_stream >> method >> path >> proto;

    // Responde a CORS preflight OPTIONS
    if (method == "OPTIONS") {
        std::stringstream resp;
        resp << "HTTP/1.1 204 No Content\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n\r\n";
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    // Separa rota limpa e query string
    std::string clean_path = path;
    std::string query_str;
    size_t q_pos = clean_path.find('?');
    if (q_pos != std::string::npos) {
        query_str = clean_path.substr(q_pos + 1);
        clean_path = clean_path.substr(0, q_pos);
    }

    if (method == "GET" && clean_path == "/") {
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << WEB_UI_HTML.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << WEB_UI_HTML;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "GET" && clean_path == "/api/status") {
        ProcessStatus st = proc_mgr_.get_status();
        std::vector<std::string> logs = proc_mgr_.get_recent_logs(50);

        nlohmann::json j;
        j["running"] = st.running;
        j["mode"] = st.mode;
        j["current_channel"] = st.current_channel;
        j["current_name"] = st.current_name;
        j["rssi"] = st.rssi;
        j["ofdm"] = st.ofdm;
        j["tmcc"] = st.tmcc;
        j["ts_packets"] = st.ts_packets;
        j["service_name"] = st.service_name;
        j["logs"] = logs;

        std::string body = j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "GET" && clean_path == "/api/channels") {
        channel_mgr_.load();
        const auto& ch_list = channel_mgr_.get_channels();

        nlohmann::json j;
        j["channels"] = nlohmann::json::array();
        for (const auto& ch : ch_list) {
            nlohmann::json c;
            c["channel"] = ch.channel_number;
            c["name"] = ch.name;
            c["frequency_hz"] = ch.frequency_hz;
            c["rssi_dbfs"] = ch.rssi_dbfs;
            c["service_id"] = ch.service_id;
            c["video_codec"] = ch.video_codec;
            c["audio_codec"] = ch.audio_codec;
            j["channels"].push_back(c);
        }

        std::string body = j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "POST" && clean_path == "/api/scan") {
        bool ok = proc_mgr_.start_scan(14, 45);
        nlohmann::json j;
        j["success"] = ok;

        std::string body = j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "POST" && clean_path == "/api/play") {
        int ch = 0;
        std::string name;

        // 1. Extrai da query string se fornecido (?channel=X&name=Y)
        std::string q_ch = get_query_param(query_str, "channel");
        if (q_ch.empty()) q_ch = get_query_param(query_str, "ch");
        if (!q_ch.empty()) {
            try { ch = std::stoi(q_ch); } catch (...) {}
        }
        std::string q_name = get_query_param(query_str, "name");
        if (!q_name.empty()) name = q_name;

        // 2. Extrai do corpo JSON
        size_t b_pos = request.find("\r\n\r\n");
        if (b_pos != std::string::npos) {
            std::string body_str = request.substr(b_pos + 4);
            try {
                auto j = nlohmann::json::parse(body_str);
                if (ch == 0) ch = j.value("channel", 0);
                if (name.empty()) name = j.value("name", "");
            } catch (...) {}
        }

        bool ok = proc_mgr_.start_play(ch, name, udp_stream_port_);
        psi_parser_ = PsiParser(); // Reseta identificacao de servicos/PIDs para o novo canal
        nlohmann::json res_j;
        res_j["success"] = ok;

        std::string body = res_j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "POST" && clean_path == "/api/stop") {
        proc_mgr_.stop();
        nlohmann::json j;
        j["success"] = true;

        std::string body = j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    if (method == "POST" && clean_path == "/api/vlc") {
        bool launched = false;
#ifdef _WIN32
        const char* vlc_paths[] = {
            "C:\\Program Files\\VideoLAN\\VLC\\vlc.exe",
            "C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe"
        };
        std::string vlc_bin;
        for (const auto& p : vlc_paths) {
            if (std::filesystem::exists(p)) {
                vlc_bin = p;
                break;
            }
        }
        if (!vlc_bin.empty()) {
            std::string udp_url = "udp://@127.0.0.1:" + std::to_string(udp_stream_port_);
            HINSTANCE hInst = ShellExecuteA(NULL, "open", vlc_bin.c_str(), udp_url.c_str(), NULL, SW_SHOWNORMAL);
            launched = (reinterpret_cast<INT_PTR>(hInst) > 32);
        }
#endif
        nlohmann::json j;
        j["success"] = launched;

        std::string body = j.dump();
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);
        CLOSE_SOCK(client_sock);
        return;
    }

    // Servidor de fragmentos e playlists HLS (/hls/live.m3u8, /hls/live0.ts, etc.)
    if (method == "GET" && clean_path.rfind("/hls/", 0) == 0) {
        std::string filename = clean_path.substr(5);
        if (filename.empty() || filename.find("..") != std::string::npos ||
            filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
            std::string err_resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client_sock, err_resp.data(), static_cast<int>(err_resp.size()), 0);
            CLOSE_SOCK(client_sock);
            return;
        }

        std::filesystem::path file_path = std::filesystem::path(proc_mgr_.get_hls_dir()) / filename;

        // Se for o manifesto principal e o FFmpeg ainda estiver gerando o primeiro segmento,
        // aguarda ate 3 segundos antes de retornar 404, evitando falha prematura no navegador.
        if (!std::filesystem::exists(file_path) && filename == "live.m3u8") {
            for (int wait_i = 0; wait_i < 30; ++wait_i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (std::filesystem::exists(file_path)) break;
            }
        }

        if (!std::filesystem::exists(file_path)) {
            std::string nf_resp = "HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client_sock, nf_resp.data(), static_cast<int>(nf_resp.size()), 0);
            CLOSE_SOCK(client_sock);
            return;
        }

        std::string mime = "application/octet-stream";
        if (filename.rfind(".m3u8") != std::string::npos) {
            mime = "application/vnd.apple.mpegurl";
        } else if (filename.rfind(".ts") != std::string::npos) {
            mime = "video/mp2t";
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::string nf_resp = "HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client_sock, nf_resp.data(), static_cast<int>(nf_resp.size()), 0);
            CLOSE_SOCK(client_sock);
            return;
        }

        file.seekg(0, std::ios::end);
        size_t file_size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<char> file_data(file_size);
        file.read(file_data.data(), file_size);

        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: " << mime << "\r\n"
             << "Content-Length: " << file_size << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
             << "Connection: close\r\n\r\n";
        std::string header_str = resp.str();
        send(client_sock, header_str.data(), static_cast<int>(header_str.size()), 0);
        if (file_size > 0) {
            send(client_sock, file_data.data(), static_cast<int>(file_size), 0);
        }
        CLOSE_SOCK(client_sock);
        return;
    }

    // Stream TS contínuo em memória (Live Stream)
    if (method == "GET" && clean_path == "/stream.ts") {
        std::stringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: video/mp2t\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Cache-Control: no-cache, no-store\r\n"
             << "Connection: close\r\n\r\n";
        std::string s = resp.str();
        send(client_sock, s.data(), static_cast<int>(s.size()), 0);

        // Registra o cliente na lista de ouvintes UDP
        {
            std::lock_guard<std::mutex> lock(stream_clients_mtx_);
            stream_clients_.insert(client_sock);
        }
        // O socket permanece aberto e alimentado pela thread udp_relay_loop
        return;
    }

    // 404 Not Found
    std::string nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send(client_sock, nf.data(), static_cast<int>(nf.size()), 0);
    CLOSE_SOCK(client_sock);
}

} // namespace isdbt
