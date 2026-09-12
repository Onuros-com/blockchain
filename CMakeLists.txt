cmake_minimum_required(VERSION 3.20)
project(OnurosLocalBlockchain LANGUAGES C CXX)
find_package(OpenSSL QUIET)

option(ONUROS_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
set(ONUROS_ORCHARD_FFI_LIBRARY "" CACHE FILEPATH
    "Path to libonuros_orchard_ffi shared/static library")

add_library(onuros_core INTERFACE)
target_include_directories(onuros_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_compile_features(onuros_core INTERFACE cxx_std_17)
if(WIN32)
  target_link_libraries(onuros_core INTERFACE ws2_32)
endif()

set(ONUROS_KAWPOW_ROOT
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ravencoin-kawpow/src)
add_library(onuros_kawpow STATIC
  core/src/kawpow.cpp
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/ethash/ethash.cpp
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/ethash/progpow.cpp
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/ethash/primes.c
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/keccak/keccak.c
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/keccak/keccakf1600.c
  ${ONUROS_KAWPOW_ROOT}/crypto/ethash/lib/keccak/keccakf800.c)
target_include_directories(onuros_kawpow PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/core/include
  ${ONUROS_KAWPOW_ROOT})
target_compile_features(onuros_kawpow PUBLIC cxx_std_17)
if(ONUROS_ORCHARD_FFI_LIBRARY)
  target_compile_definitions(onuros_core INTERFACE ONUROS_ORCHARD_FFI_ENABLED=1)
  target_link_libraries(onuros_core INTERFACE "${ONUROS_ORCHARD_FFI_LIBRARY}")
endif()

if(ONUROS_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "Combined ASan/UBSan requires GCC or Clang")
  endif()
  target_compile_options(onuros_kawpow PRIVATE
    -fsanitize=address,undefined -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
endif()

set(ONUROS_TESTS economics_test block_format_test compact_block_relay_test p2p_protocol_test p2p_transport_test peer_manager_test peer_store_test peer_event_loop_test block_transfer_test stage7_relay_test stage7_node_admission_test stage7_transaction_frame_test stage7_orchard_network_test header_sync_test download_coordinator_test mini_node_history_test block_validation_test chain_index_test difficulty_test block_store_test active_chain_test local_node_test private_admission_test private_transaction_test private_fuzz_test orchard_ffi_backend_test persistent_shielded_state_test private_reward_test private_mempool_test private_block_test private_node_commit_test kawpow_test mining_endpoint_test)
if(ONUROS_ORCHARD_FFI_LIBRARY)
  list(APPEND ONUROS_TESTS private_node_orchard_pipeline_test)
endif()
foreach(target ${ONUROS_TESTS})
  add_executable(${target} tests/${target}.cpp)
  target_link_libraries(${target} PRIVATE onuros_core)
endforeach()
target_link_libraries(kawpow_test PRIVATE onuros_kawpow)
target_link_libraries(mining_endpoint_test PRIVATE onuros_kawpow)

add_executable(onuros_local_node apps/local_node_main.cpp)
target_link_libraries(onuros_local_node PRIVATE onuros_core)
add_executable(onuros_private_tps_benchmark apps/private_tps_benchmark.cpp)
target_link_libraries(onuros_private_tps_benchmark PRIVATE onuros_core)
add_executable(onuros_stage7_bandwidth_benchmark apps/stage7_bandwidth_benchmark.cpp)
target_link_libraries(onuros_stage7_bandwidth_benchmark PRIVATE onuros_core)
add_executable(onuros_stage7_network_node apps/stage7_network_node.cpp)
target_link_libraries(onuros_stage7_network_node PRIVATE onuros_core)
add_executable(onuros_stage7_private_relay_node
  apps/stage7_private_relay_node.cpp)
target_link_libraries(onuros_stage7_private_relay_node PRIVATE onuros_core)
add_executable(onuros_mining_endpoint apps/mining_endpoint_main.cpp)
target_link_libraries(onuros_mining_endpoint PRIVATE onuros_core onuros_kawpow)
if(OpenSSL_FOUND)
  add_executable(tls_transport_test tests/tls_transport_test.cpp)
  add_executable(onuros_stage7_tls_probe apps/stage7_tls_probe.cpp)
  foreach(target tls_transport_test onuros_stage7_tls_probe)
    target_link_libraries(${target} PRIVATE onuros_core OpenSSL::SSL OpenSSL::Crypto)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(ONUROS_SANITIZERS)
      if(MSVC)
        message(FATAL_ERROR "Combined ASan/UBSan requires GCC or Clang")
      endif()
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
  endforeach()
  if(ONUROS_ORCHARD_FFI_LIBRARY)
    add_executable(onuros_stage7_private_load_node
      apps/stage7_private_load_node.cpp)
    target_link_libraries(onuros_stage7_private_load_node PRIVATE
      onuros_core OpenSSL::SSL OpenSSL::Crypto)
    set_target_properties(onuros_stage7_private_load_node PROPERTIES
      CXX_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(onuros_stage7_private_load_node PRIVATE
        /W4 /WX /permissive-)
    else()
      target_compile_options(onuros_stage7_private_load_node PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()
endif()

foreach(target ${ONUROS_TESTS} onuros_local_node onuros_private_tps_benchmark onuros_stage7_bandwidth_benchmark onuros_stage7_network_node onuros_stage7_private_relay_node onuros_mining_endpoint)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(ONUROS_SANITIZERS)
    if(MSVC)
      message(FATAL_ERROR "Combined ASan/UBSan requires GCC or Clang")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
  endif()
endforeach()

enable_testing()
add_test(NAME economics COMMAND economics_test)
add_test(NAME block_format COMMAND block_format_test)
add_test(NAME compact_block_relay COMMAND compact_block_relay_test)
add_test(NAME p2p_protocol COMMAND p2p_protocol_test)
add_test(NAME p2p_transport COMMAND p2p_transport_test)
add_test(NAME peer_manager COMMAND peer_manager_test)
add_test(NAME peer_store COMMAND peer_store_test)
add_test(NAME peer_event_loop COMMAND peer_event_loop_test)
add_test(NAME block_transfer COMMAND block_transfer_test)
add_test(NAME stage7_relay COMMAND stage7_relay_test)
add_test(NAME stage7_node_admission COMMAND stage7_node_admission_test)
add_test(NAME stage7_transaction_frame COMMAND stage7_transaction_frame_test)
add_test(NAME stage7_orchard_network COMMAND stage7_orchard_network_test)
add_test(NAME header_sync COMMAND header_sync_test)
add_test(NAME download_coordinator COMMAND download_coordinator_test)
add_test(NAME mini_node_history COMMAND mini_node_history_test)
add_test(NAME block_validation COMMAND block_validation_test)
add_test(NAME chain_index COMMAND chain_index_test)
add_test(NAME difficulty COMMAND difficulty_test)
add_test(NAME block_store COMMAND block_store_test)
add_test(NAME active_chain COMMAND active_chain_test)
add_test(NAME local_node COMMAND local_node_test)
add_test(NAME private_admission COMMAND private_admission_test)
add_test(NAME private_transaction COMMAND private_transaction_test)
add_test(NAME private_fuzz COMMAND private_fuzz_test)
add_test(NAME orchard_ffi_backend COMMAND orchard_ffi_backend_test)
add_test(NAME persistent_shielded_state COMMAND persistent_shielded_state_test)
add_test(NAME private_reward COMMAND private_reward_test)
add_test(NAME private_mempool COMMAND private_mempool_test)
add_test(NAME private_block COMMAND private_block_test)
add_test(NAME private_node_commit COMMAND private_node_commit_test)
add_test(NAME kawpow COMMAND kawpow_test)
add_test(NAME mining_endpoint COMMAND mining_endpoint_test)
add_test(NAME local_node_cli COMMAND onuros_local_node
         --data ${CMAKE_CURRENT_BINARY_DIR}/onuros-ci-node.db --blocks 3)
add_test(NAME stage7_block_limit COMMAND onuros_stage7_bandwidth_benchmark)
set_tests_properties(stage7_block_limit PROPERTIES TIMEOUT 120)
if(NOT WIN32)
  add_test(NAME amd_kawpow_log_parser COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/tests/amd_kawpow_log_parser_test.sh
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-amd-kawpow-log.sh)
  add_test(NAME stage7_performance_manifest COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage7_performance_manifest_test.sh
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-stage7-performance-evidence.sh)
  add_test(NAME stage7_three_process COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-stage7-three-process.sh
           $<TARGET_FILE:onuros_stage7_network_node>
           ${CMAKE_CURRENT_BINARY_DIR}/stage7-three-process)
  add_test(NAME stage7_private_relay COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-stage7-private-relay.sh
           $<TARGET_FILE:onuros_stage7_private_relay_node>
           ${CMAKE_CURRENT_BINARY_DIR}/stage7-private-relay)
  add_test(NAME mining_endpoint_loopback COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-mining-endpoint-loopback.sh
           $<TARGET_FILE:onuros_mining_endpoint>
           ${CMAKE_CURRENT_BINARY_DIR}/mining-endpoint-loopback)
endif()
if(OpenSSL_FOUND AND NOT WIN32)
  add_test(NAME stage7_mutual_tls COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-stage7-tls-loopback.sh
           $<TARGET_FILE:tls_transport_test>)
  add_test(NAME stage7_independent_tls_loopback COMMAND bash
           ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-stage7-independent-tls-loopback.sh
           $<TARGET_FILE:onuros_stage7_tls_probe>)
endif()
if(ONUROS_ORCHARD_FFI_LIBRARY)
  add_test(NAME private_node_orchard_pipeline
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-private-node-orchard-test.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/crypto/orchard-ffi/target/release/private_verify_benchmark
                   $<TARGET_FILE:private_node_orchard_pipeline_test>)
endif()
