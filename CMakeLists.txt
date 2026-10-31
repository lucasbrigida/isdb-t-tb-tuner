cmake_minimum_required(VERSION 3.20)
project(isdbtb_rx VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Local dependency roots
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")
set(SETUP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build-setup/rtl-sdr-64bit-20261018")
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
    src/dsp/reed_solomon.cpp
    src/dsp/descrambler.cpp
    src/dsp/viterbi_decoder.cpp
    src/dsp/ofdm_sync.cpp
    src/streaming/udp_streamer.cpp
    src/mpegts/psi_parser.cpp
    src/cli/channel_manager.cpp
)

add_executable(isdbtb_rx ${SOURCES})

# Target libraries
target_link_libraries(isdbtb_rx PRIVATE
    rtlsdr
    fftw3f-3
    volk
)

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

target_link_libraries(test_ofdm_sync PRIVATE
    fftw3f-3
    volk
)

# CTest Integration
enable_testing()
add_test(NAME ReedSolomon COMMAND test_reed_solomon)
add_test(NAME Descrambler COMMAND test_descrambler)
add_test(NAME Viterbi COMMAND test_viterbi)
add_test(NAME FecPipeline COMMAND test_fec_pipeline)
add_test(NAME OfdmSync COMMAND test_ofdm_sync)

if(WIN32)
    target_link_libraries(isdbtb_rx PRIVATE ws2_32)
    target_compile_definitions(isdbtb_rx PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Copy runtime DLLs to output directory for instant execution
if(WIN32)
    set(DLLS_TO_COPY
        "${FFTW3_ROOT}/libfftw3f-3.dll"
        "${VOLK_ROOT}/bin/libvolk.dll"
    )

    if(EXISTS "${SETUP_DIR}/librtlsdr.dll")
        list(APPEND DLLS_TO_COPY
            "${SETUP_DIR}/librtlsdr.dll"
            "${SETUP_DIR}/libusb-1.0.dll"
            "${SETUP_DIR}/libwinpthread-1.dll"
        )
    else()
        list(APPEND DLLS_TO_COPY
            "${RTLSDR_ROOT}/x64/rtlsdr.dll"
            "${RTLSDR_ROOT}/x64/pthreadVC2.dll"
            "${RTLSDR_ROOT}/x64/msvcr100.dll"
        )
    endif()

    add_custom_command(TARGET isdbtb_rx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DLLS_TO_COPY}
            $<TARGET_FILE_DIR:isdbtb_rx>
    )
endif()

