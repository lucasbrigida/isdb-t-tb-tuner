#include "cli/epg_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <chrono>

using namespace isdbt;

static void test_prune_and_valid_now() {
    std::cout << "[Test] EpgManager prune_expired e has_valid_now...\n";
    std::string test_file = "test_epg_prune.json";
    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);

    EpgManager mgr(test_file);
    auto now_tp = std::chrono::system_clock::now();

    // Event 1: Expired (ended 30 minutes ago)
    EpgEvent e1;
    e1.event_id = 101;
    e1.title = "Programa Passado";
    e1.start_tp = now_tp - std::chrono::minutes(90);
    e1.end_tp = now_tp - std::chrono::minutes(30);
    e1.duration_sec = 3600;

    // Event 2: In progress (started 20 min ago, ends in 40 min)
    EpgEvent e2;
    e2.event_id = 102;
    e2.title = "Jornal do Meio Dia";
    e2.start_tp = now_tp - std::chrono::minutes(20);
    e2.end_tp = now_tp + std::chrono::minutes(40);
    e2.duration_sec = 3600;

    // Event 3: Future (starts in 40 min)
    EpgEvent e3;
    e3.event_id = 103;
    e3.title = "Filme da Tarde";
    e3.start_tp = now_tp + std::chrono::minutes(40);
    e3.end_tp = now_tp + std::chrono::minutes(160);
    e3.duration_sec = 7200;

    mgr.enrich_channel_events(18, "Globo HD", 497142857, true, {e1, e2, e3}, now_tp);

    // Event 1 should have been pruned
    auto* ch = mgr.get_channel(18);
    assert(ch != nullptr);
    assert(ch->events.size() == 2);
    assert(ch->events[0].event_id == 102);
    assert(ch->events[0].is_now == true);
    assert(ch->events[1].event_id == 103);
    assert(ch->events[1].is_next == true);

    EpgEvent now_evt, next_evt;
    bool has_now = mgr.has_valid_now(18, now_tp, &now_evt, &next_evt);
    assert(has_now == true);
    assert(now_evt.title == "Jornal do Meio Dia");
    assert(next_evt.title == "Filme da Tarde");

    // Canal inexistente
    assert(!mgr.has_valid_now(99, now_tp));

    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);
}

static void test_enrichment() {
    std::cout << "[Test] EpgManager enriquecimento incremental de metadados...\n";
    std::string test_file = "test_epg_enrich.json";
    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);

    EpgManager mgr(test_file);
    auto now_tp = std::chrono::system_clock::now();

    // Evento inicial simples (apenas titulo)
    EpgEvent e_base;
    e_base.event_id = 200;
    e_base.title = "Novela das Oito";
    e_base.start_tp = now_tp - std::chrono::minutes(10);
    e_base.end_tp = now_tp + std::chrono::minutes(50);
    e_base.duration_sec = 3600;

    // Evento futuro antigo mantido em cache (ex: grade de 6 horas a frente)
    EpgEvent e_future_cache;
    e_future_cache.event_id = 205;
    e_future_cache.title = "Madrugada com Noticias";
    e_future_cache.start_tp = now_tp + std::chrono::hours(5);
    e_future_cache.end_tp = now_tp + std::chrono::hours(6);
    e_future_cache.duration_sec = 3600;

    mgr.enrich_channel_events(24, "Cultura HD", 533142857, true, {e_base, e_future_cache}, now_tp);

    // Nova leitura de RF traz sinopse, genero, rating para o evento 200 e um novo evento imediato 201
    EpgEvent e_update;
    e_update.event_id = 200;
    e_update.title = "Novela das Oito - Capitulo Especial";
    e_update.short_desc = "Resumo do capitulo de hoje.";
    e_update.extended_desc = "Sinopse completa com detalhes dos atores e enredo dramatico.";
    e_update.genre = "Telenovela";
    e_update.rating = "12 anos";
    e_update.start_tp = e_base.start_tp;
    e_update.end_tp = e_base.end_tp;

    EpgEvent e_new_rf;
    e_new_rf.event_id = 201;
    e_new_rf.title = "Jornal da Cultura";
    e_new_rf.start_tp = e_base.end_tp;
    e_new_rf.end_tp = e_base.end_tp + std::chrono::hours(1);
    e_new_rf.duration_sec = 3600;

    mgr.enrich_channel_events(24, "Cultura HD", 533142857, true, {e_update, e_new_rf}, now_tp);

    auto* ch = mgr.get_channel(24);
    assert(ch != nullptr);
    // Deve conter os 3 eventos: o enriquecido 200, o novo 201, e o 205 que ja estava em cache
    assert(ch->events.size() == 3);
    assert(ch->events[0].event_id == 200);
    assert(ch->events[0].title == "Novela das Oito - Capitulo Especial");
    assert(ch->events[0].short_desc == "Resumo do capitulo de hoje.");
    assert(ch->events[0].extended_desc == "Sinopse completa com detalhes dos atores e enredo dramatico.");
    assert(ch->events[0].genre == "Telenovela");
    assert(ch->events[0].rating == "12 anos");

    assert(ch->events[1].event_id == 201);
    assert(ch->events[1].title == "Jornal da Cultura");

    assert(ch->events[2].event_id == 205);
    assert(ch->events[2].title == "Madrugada com Noticias");

    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);
}

static void test_save_load_json() {
    std::cout << "[Test] EpgManager serializacao e carregamento JSON UTF-8...\n";
    std::string test_file = "test_epg_io.json";
    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);

    auto now_tp = std::chrono::system_clock::now();

    {
        EpgManager mgr(test_file);
        EpgEvent e;
        e.event_id = 301;
        e.title = "Globo Notícias & Opinião";
        e.short_desc = "As notícias com análise e acentuação brasileira (ção, não, você).";
        e.extended_desc = "Apresentação ao vivo com comentários críticos.";
        e.genre = "Jornalismo";
        e.rating = "Livre";
        e.start_tp = now_tp - std::chrono::minutes(15);
        e.end_tp = now_tp + std::chrono::minutes(45);
        e.duration_sec = 3600;

        mgr.enrich_channel_events(18, "Globo HD", 497142857, true, {e}, now_tp);
        bool ok = mgr.save();
        assert(ok == true);
    }

    // Carrega em nova instancia
    {
        EpgManager mgr(test_file);
        bool ok = mgr.load();
        assert(ok == true);

        auto* ch = mgr.get_channel(18);
        assert(ch != nullptr);
        assert(ch->channel_name == "Globo HD");
        assert(ch->events.size() == 1);
        assert(ch->events[0].title == "Globo Notícias & Opinião");
        assert(ch->events[0].short_desc == "As notícias com análise e acentuação brasileira (ção, não, você).");
        assert(ch->events[0].extended_desc == "Apresentação ao vivo com comentários críticos.");
        assert(ch->events[0].genre == "Jornalismo");
        assert(ch->events[0].rating == "Livre");
        assert(ch->events[0].is_now == true);
    }

    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);
}

static void test_view_temporal_shift() {
    std::cout << "[Test] EpgManager simulacao temporal de visualizacao (--view)...\n";
    std::string test_file = "test_epg_view_shift.json";
    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);

    auto t0 = std::chrono::system_clock::now();

    EpgEvent e_morning;
    e_morning.event_id = 1;
    e_morning.title = "Manha Noticia";
    e_morning.start_tp = t0;
    e_morning.end_tp = t0 + std::chrono::hours(1);
    e_morning.duration_sec = 3600;

    EpgEvent e_afternoon;
    e_afternoon.event_id = 2;
    e_afternoon.title = "Tarde Show";
    e_afternoon.start_tp = t0 + std::chrono::hours(1);
    e_afternoon.end_tp = t0 + std::chrono::hours(3);
    e_afternoon.duration_sec = 7200;

    {
        EpgManager mgr(test_file);
        mgr.enrich_channel_events(14, "RBI HD", 473142857, true, {e_morning, e_afternoon}, t0);
        mgr.save();
    }

    // No instante t0 + 1h30:
    // "Manha Noticia" encerrou e foi podado como expirado.
    // "Tarde Show" passa a ser o programa ativo no ar (is_now == true).
    {
        EpgManager mgr(test_file);
        mgr.load();
        auto t_future = t0 + std::chrono::minutes(90);
        mgr.prune_expired(t_future);

        EpgEvent now_evt, next_evt;
        bool valid = mgr.has_valid_now(14, t_future, &now_evt, &next_evt);
        assert(valid == true);
        assert(now_evt.title == "Tarde Show");
        assert(now_evt.is_now == true);

        auto* ch = mgr.get_channel(14);
        assert(ch != nullptr);
        assert(ch->events.size() == 1);
        assert(ch->events[0].title == "Tarde Show");
    }

    if (std::filesystem::exists(test_file)) std::filesystem::remove(test_file);
}

int main() {
    try {
        test_prune_and_valid_now();
        test_enrichment();
        test_save_load_json();
        test_view_temporal_shift();
        std::cout << "[PASS] Todos os testes de EpgManager passaram com sucesso!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FALHA] Excecao: " << e.what() << "\n";
        return 1;
    }
}

