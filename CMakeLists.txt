cmake_minimum_required(VERSION 3.20)
project(isdbtb_rx VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opcoes de otimizacao para builds de Release
option(ISDBT_ENABLE_FAST_MATH "Habilitar otimizacao de ponto flutuante rapido (-ffast-math / /fp:fast)" OFF)

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
    src/cli/channel_manager.cpp
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/dsp/oneseg_demodulator.cpp
    src/streaming/udp_streamer.cpp
    src/mpegts/psi_parser.cpp
    src/mpegts/ts_generator.cpp
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
)

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

    if(EXISTS "${SETUP_DIR}/librtlsdr.dll")
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

    if(DLLS_TO_COPY)
        add_custom_command(TARGET isdbtb_rx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${DLLS_TO_COPY}
                $<TARGET_FILE_DIR:isdbtb_rx>
        )
    endif()
endif()
