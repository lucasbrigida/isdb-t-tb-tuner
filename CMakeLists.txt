cmake_minimum_required(VERSION 3.20)
project(isdbtb_rx VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0600)
endif()

# Opcoes de otimizacao para builds de Release
option(ISDBT_ENABLE_FAST_MATH "Habilitar otimizacao de ponto flutuante rapido (-ffast-math / /fp:fast)" OFF)
option(ISDBT_ENABLE_GRISDBT_SYNC "Habilitar algoritmo de sincronizacao robusto do gr-isdbt (--ofdm=2)" OFF)
set(TARGET_ARCH "auto" CACHE STRING "Arquitetura alvo: auto, x64 ou x86")

if(TARGET_ARCH STREQUAL "x86" OR TARGET_ARCH STREQUAL "32")
    message(STATUS "Configurando compilacao para 32 bits (x86)")
    if(MSVC)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
            message(FATAL_ERROR "Configure a arquitetura x86 do MSVC com -A Win32.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-m32 -msse2 -mfpmath=sse)
        add_link_options(-m32)
    else()
        message(FATAL_ERROR "TARGET_ARCH=x86 requer MSVC Win32 ou um compilador GNU/Clang.")
    endif()
    set(IS_32BIT TRUE)
elseif(TARGET_ARCH STREQUAL "x64" OR TARGET_ARCH STREQUAL "64")
    message(STATUS "Configurando compilacao para 64 bits (x64)")
    if(MSVC)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR "Configure a arquitetura x64 do MSVC com -A x64.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-m64)
        add_link_options(-m64)
    else()
        message(FATAL_ERROR "TARGET_ARCH=x64 requer MSVC x64 ou um compilador GNU/Clang.")
    endif()
    set(IS_32BIT FALSE)
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(IS_32BIT TRUE)
        message(STATUS "Arquitetura detectada automaticamente: 32 bits (x86)")
    else()
        set(IS_32BIT FALSE)
        message(STATUS "Arquitetura detectada automaticamente: 64 bits (x64)")
    endif()
endif()

set(HASH_LIBS "")
if(NOT WIN32)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    set(HASH_LIBS OpenSSL::Crypto)
endif()

if(ISDBT_ENABLE_GRISDBT_SYNC)
    add_compile_definitions(ISDBT_ENABLE_GRISDBT_SYNC=1)
    message(STATUS "Modo sincronizador robusto gr-isdbt (--ofdm=2) HABILITADO")
    set(GRISDBT_SYNC_SOURCES src/sync/robust_grisdbt_sync.cpp)
else()
    set(GRISDBT_SYNC_SOURCES "")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    if(ISDBT_ENABLE_FAST_MATH)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /fp:fast")
    endif()
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    if(ISDBT_ENABLE_FAST_MATH)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math")
    endif()
endif()

# Interprocedural Optimization (LTO)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    message(STATUS "IPO / LTO suportado e habilitado para Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(STATUS "IPO / LTO nao suportado no compilador atual: ${ipo_error}")
endif()

# Local dependency roots
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")
set(SETUP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rtl-sdr")
set(RTLSDR_ROOT "${DEPS_DIR}/rtlsdr")
set(FFTW3_ROOT "${DEPS_DIR}/fftw3")
set(VOLK_ROOT "${DEPS_DIR}/volk-install")

# Include directories
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/third_party"
    "${RTLSDR_ROOT}/include"
    "${FFTW3_ROOT}"
    "${VOLK_ROOT}/include"
)

# Link directories
link_directories(
    "${SETUP_DIR}"
    "${RTLSDR_ROOT}/x64"
    "${FFTW3_ROOT}"
    "${VOLK_ROOT}/lib"
)

# Sources
set(SOURCES
    src/main.cpp
    src/radio/frequency_plan.cpp
    src/radio/rtlsdr_device.cpp
    src/platform/platform_utils.cpp
    src/cli/cmd_probe.cpp
    src/cli/cmd_scan.cpp
    src/cli/cmd_list.cpp
    src/cli/cmd_play.cpp
    src/cli/cmd_epg.cpp
    src/cli/epg_manager.cpp
    src/cli/channel_manager.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/streaming/udp_streamer.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
    src/mpegts/ts_generator.cpp
    src/sync/van_de_beek_sync.cpp
    src/sync/sync_factory.cpp
    src/utils/ascii_table.cpp
    ${GRISDBT_SYNC_SOURCES}
)

add_executable(isdbtb_rx ${SOURCES})

# Target libraries (link external DLLs if deps/ directory is populated; otherwise use bundled headers/dynamic loader)
if(EXISTS "${FFTW3_ROOT}/libfftw3f-3.dll")
    set(DSP_EXTERNAL_LIBS fftw3f-3 volk)
    target_link_libraries(isdbtb_rx PRIVATE rtlsdr ${DSP_EXTERNAL_LIBS})
else()
    set(DSP_EXTERNAL_LIBS "")
endif()

# Testes Unitarios
add_executable(test_reed_solomon
    tests/test_reed_solomon.cpp
    src/dsp/reed_solomon.cpp
)

add_executable(test_descrambler
    tests/test_descrambler.cpp
    src/dsp/descrambler.cpp
)

add_executable(test_viterbi
    tests/test_viterbi.cpp
    src/dsp/viterbi_decoder.cpp
)

add_executable(test_fec_pipeline
    tests/test_fec_pipeline.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
)

add_executable(test_ofdm_sync
    tests/test_ofdm_sync.cpp
    src/dsp/ofdm_sync.cpp
)

target_link_libraries(test_ofdm_sync PRIVATE ${DSP_EXTERNAL_LIBS})

add_executable(test_udp_streamer
    tests/test_udp_streamer.cpp
    src/streaming/udp_streamer.cpp
)

if(WIN32)
    target_link_libraries(test_udp_streamer PRIVATE ws2_32)
endif()

add_executable(test_oneseg_e2e
    tests/test_oneseg_e2e.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
)

target_link_libraries(test_oneseg_e2e PRIVATE ${DSP_EXTERNAL_LIBS})

add_executable(test_iq_stream_buffer
    tests/test_iq_stream_buffer.cpp
)

add_executable(test_spsc_queue
    tests/test_spsc_queue.cpp
)

add_executable(test_delay_line
    tests/test_delay_line.cpp
)

add_executable(test_reset_recovery
    tests/test_reset_recovery.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
)
target_link_libraries(test_reset_recovery PRIVATE ${DSP_EXTERNAL_LIBS})

add_executable(test_sync_state_and_csi
    tests/test_sync_state_and_csi.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
)
target_link_libraries(test_sync_state_and_csi PRIVATE ${DSP_EXTERNAL_LIBS})

add_executable(test_frequency_plan
    tests/test_frequency_plan.cpp
    src/radio/frequency_plan.cpp
)

add_executable(test_channel_manager
    tests/test_channel_manager.cpp
    src/cli/channel_manager.cpp
)

add_executable(test_psi_parser
    tests/test_psi_parser.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)

add_executable(test_epg_manager
    tests/test_epg_manager.cpp
    src/cli/epg_manager.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)

add_executable(test_arib_b24
    tests/test_arib_b24.cpp
    src/mpegts/arib_b24_decoder.cpp
)

add_executable(freeze_baseline_ofdm1
    tools/freeze_baseline_ofdm1.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)
target_link_libraries(freeze_baseline_ofdm1 PRIVATE ${DSP_EXTERNAL_LIBS} ${HASH_LIBS})

add_executable(test_cli_ofdm_flag
    tests/test_cli_ofdm_flag.cpp
)

add_executable(test_sync_regression_scan
    tests/test_sync_regression_scan.cpp
)

add_executable(test_bench_ascii_table
    tests/test_bench_ascii_table.cpp
    src/utils/ascii_table.cpp
)

add_executable(test_regression_ofdm1
    tests/test_regression_ofdm1.cpp
    src/sync/van_de_beek_sync.cpp
    src/sync/sync_factory.cpp
    ${GRISDBT_SYNC_SOURCES}
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)
target_link_libraries(test_regression_ofdm1 PRIVATE ${DSP_EXTERNAL_LIBS} ${HASH_LIBS})

add_executable(test_sync_grisdbt_robust
    tests/test_sync_grisdbt_robust.cpp
    src/sync/robust_grisdbt_sync.cpp
)

add_executable(test_regression_ofdm2
    tests/test_regression_ofdm2.cpp
    src/sync/robust_grisdbt_sync.cpp
    src/sync/van_de_beek_sync.cpp
    src/sync/sync_factory.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)
target_link_libraries(test_regression_ofdm2 PRIVATE ${DSP_EXTERNAL_LIBS} ${HASH_LIBS})

# Target dedicado de Benchmark (R17)
add_executable(bench_sync
    bench/bench_sync_main.cpp
    bench/bench_metrics.cpp
    bench/bench_report.cpp
    src/utils/ascii_table.cpp
    src/sync/van_de_beek_sync.cpp
    src/sync/robust_grisdbt_sync.cpp
    src/sync/sync_factory.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/arib_b24_decoder.cpp
)
target_link_libraries(bench_sync PRIVATE ${DSP_EXTERNAL_LIBS})
target_compile_definitions(bench_sync PRIVATE ISDBT_ENABLE_GRISDBT_SYNC=1)
if(WIN32)
    target_link_libraries(bench_sync PRIVATE psapi)
endif()
set_target_properties(bench_sync PROPERTIES LABELS "bench")

# CTest Integration
enable_testing()
add_test(NAME ReedSolomon COMMAND test_reed_solomon)
add_test(NAME Descrambler COMMAND test_descrambler)
add_test(NAME Viterbi COMMAND test_viterbi)
add_test(NAME FecPipeline COMMAND test_fec_pipeline)
add_test(NAME OfdmSync COMMAND test_ofdm_sync)
add_test(NAME OneSegE2E COMMAND test_oneseg_e2e)
add_test(NAME IqStreamBuffer COMMAND test_iq_stream_buffer)
add_test(NAME SpscQueue COMMAND test_spsc_queue)
add_test(NAME DelayLine COMMAND test_delay_line)
add_test(NAME ResetRecovery COMMAND test_reset_recovery)
add_test(NAME SyncStateAndCsi COMMAND test_sync_state_and_csi)
add_test(NAME FrequencyPlan COMMAND test_frequency_plan)
add_test(NAME ChannelManager COMMAND test_channel_manager)
add_test(NAME PsiParser COMMAND test_psi_parser)
add_test(NAME AribB24 COMMAND test_arib_b24)
add_test(NAME EpgManager COMMAND test_epg_manager)
add_test(NAME UdpStreamer COMMAND test_udp_streamer)
add_test(NAME CliOfdmFlag COMMAND test_cli_ofdm_flag)
add_test(NAME SyncRegressionScan COMMAND test_sync_regression_scan)
add_test(NAME BenchAsciiTable COMMAND test_bench_ascii_table)
add_test(NAME RegressionOfdm1 COMMAND test_regression_ofdm1)
add_test(NAME SyncGrisdbtRobust COMMAND test_sync_grisdbt_robust)
add_test(NAME RegressionOfdm2 COMMAND test_regression_ofdm2
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/ch24_10s_iq.cu8"
    "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME BenchSync COMMAND bench_sync --compare)
set_tests_properties(BenchSync PROPERTIES LABELS "bench")

if(WIN32)
    target_link_libraries(isdbtb_rx PRIVATE ws2_32 shell32)
    target_compile_definitions(isdbtb_rx PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Copy runtime DLLs to output directory
if(WIN32)
    set(DLLS_TO_COPY "")
    if(EXISTS "${FFTW3_ROOT}/libfftw3f-3.dll")
        list(APPEND DLLS_TO_COPY
            "${FFTW3_ROOT}/libfftw3f-3.dll"
            "${VOLK_ROOT}/bin/libvolk.dll"
        )
    endif()

    if(IS_32BIT)
        if(EXISTS "${SETUP_DIR}/x86/librtlsdr.dll")
            list(APPEND DLLS_TO_COPY "${SETUP_DIR}/x86/librtlsdr.dll")
            if(EXISTS "${SETUP_DIR}/x86/libusb-1.0.dll")
                list(APPEND DLLS_TO_COPY "${SETUP_DIR}/x86/libusb-1.0.dll")
            endif()
            if(EXISTS "${SETUP_DIR}/x86/libwinpthread-1.dll")
                list(APPEND DLLS_TO_COPY "${SETUP_DIR}/x86/libwinpthread-1.dll")
            endif()
            if(EXISTS "${SETUP_DIR}/x86/libgcc_s_sjlj-1.dll")
                list(APPEND DLLS_TO_COPY "${SETUP_DIR}/x86/libgcc_s_sjlj-1.dll")
            endif()
        elseif(EXISTS "${SETUP_DIR}/librtlsdr.dll")
            list(APPEND DLLS_TO_COPY "${SETUP_DIR}/librtlsdr.dll")
        endif()
    else()
        if(EXISTS "${SETUP_DIR}/x64/librtlsdr.dll")
            list(APPEND DLLS_TO_COPY
                "${SETUP_DIR}/x64/librtlsdr.dll"
                "${SETUP_DIR}/x64/libusb-1.0.dll"
                "${SETUP_DIR}/x64/libwinpthread-1.dll"
            )
        elseif(EXISTS "${SETUP_DIR}/librtlsdr.dll")
            list(APPEND DLLS_TO_COPY
                "${SETUP_DIR}/librtlsdr.dll"
                "${SETUP_DIR}/libusb-1.0.dll"
                "${SETUP_DIR}/libwinpthread-1.dll"
            )
        elseif(EXISTS "${RTLSDR_ROOT}/x64/rtlsdr.dll")
            list(APPEND DLLS_TO_COPY
                "${RTLSDR_ROOT}/x64/rtlsdr.dll"
                "${RTLSDR_ROOT}/x64/pthreadVC2.dll"
                "${RTLSDR_ROOT}/x64/msvcr100.dll"
            )
        endif()
    endif()

    if(DLLS_TO_COPY)
        add_custom_command(TARGET isdbtb_rx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DLLS_TO_COPY}
                $<TARGET_FILE_DIR:isdbtb_rx>
        )
    endif()
endif()
