CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
TESTS = economics_test block_format_test compact_block_relay_test p2p_protocol_test p2p_transport_test peer_manager_test peer_store_test peer_event_loop_test block_transfer_test archive_retrieval_test stage7_relay_test stage7_node_admission_test stage7_transaction_frame_test stage7_orchard_network_test header_sync_test download_coordinator_test mini_node_history_test block_validation_test chain_index_test difficulty_test block_store_test active_chain_test local_node_test private_admission_test private_transaction_test compact_private_effect_test recursive_proof_gate_test private_fuzz_test orchard_ffi_backend_test persistent_shielded_state_test private_reward_test private_mempool_test private_block_test private_node_commit_test pruning_test state_snapshot_test snapshot_trust_test block_production_policy_test

.PHONY: all node network-node private-relay-node tls-probe mining-endpoint benchmark bandwidth-benchmark tls-test test kawpow-test mining-endpoint-test sanitize clean

all: node

node: build/onuros-local-node

network-node: build/onuros-stage7-network-node

private-relay-node: build/onuros-stage7-private-relay-node

tls-probe: build/onuros-stage7-tls-probe

mining-endpoint: build/onuros-mining-endpoint

benchmark: build/onuros-private-tps-benchmark

bandwidth-benchmark: build/onuros-stage7-bandwidth-benchmark

tls-test:
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include tests/tls_transport_test.cpp -o build/tls_transport_test -lssl -lcrypto
	bash scripts/run-stage7-tls-loopback.sh ./build/tls_transport_test

test: $(addprefix build/,$(TESTS))
	@for test in $(TESTS); do ./build/$$test || exit $$?; done
	@$(MAKE) --no-print-directory kawpow-test
	@$(MAKE) --no-print-directory mining-endpoint-test
	@bash tests/amd_kawpow_log_parser_test.sh scripts/validate-amd-kawpow-log.sh
	@bash tests/stage7_performance_manifest_test.sh scripts/validate-stage7-performance-evidence.sh
	@bash tests/stage7_evidence_bundle_test.sh scripts/validate-stage7-evidence-bundle.sh

kawpow-test:
	mkdir -p build/kawpow-objects
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/primes.c -o build/kawpow-objects/primes.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccak.c -o build/kawpow-objects/keccak.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf1600.c -o build/kawpow-objects/keccakf1600.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf800.c -o build/kawpow-objects/keccakf800.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c tests/kawpow_test.cpp -o build/kawpow-objects/test.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c core/src/kawpow.cpp -o build/kawpow-objects/onuros.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/ethash.cpp -o build/kawpow-objects/ethash.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/progpow.cpp -o build/kawpow-objects/progpow.o
	$(CXX) build/kawpow-objects/*.o -o build/kawpow_test
	./build/kawpow_test

mining-endpoint-test:
	mkdir -p build/mining-endpoint-objects
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/primes.c -o build/mining-endpoint-objects/primes.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccak.c -o build/mining-endpoint-objects/keccak.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf1600.c -o build/mining-endpoint-objects/keccakf1600.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf800.c -o build/mining-endpoint-objects/keccakf800.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c tests/mining_endpoint_test.cpp -o build/mining-endpoint-objects/test.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c core/src/kawpow.cpp -o build/mining-endpoint-objects/onuros.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/ethash.cpp -o build/mining-endpoint-objects/ethash.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/progpow.cpp -o build/mining-endpoint-objects/progpow.o
	$(CXX) build/mining-endpoint-objects/*.o -o build/mining_endpoint_test
	./build/mining_endpoint_test

build/onuros-local-node: apps/local_node_main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/onuros-private-tps-benchmark: apps/private_tps_benchmark.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/onuros-stage7-bandwidth-benchmark: apps/stage7_bandwidth_benchmark.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/onuros-stage7-network-node: apps/stage7_network_node.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/onuros-stage7-private-relay-node: apps/stage7_private_relay_node.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/onuros-stage7-tls-probe: apps/stage7_tls_probe.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@ -lssl -lcrypto

build/onuros-mining-endpoint: apps/mining_endpoint_main.cpp
	mkdir -p build/mining-endpoint-app-objects
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/primes.c -o build/mining-endpoint-app-objects/primes.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccak.c -o build/mining-endpoint-app-objects/keccak.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf1600.c -o build/mining-endpoint-app-objects/keccakf1600.o
	$(CC) -std=c11 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/keccak/keccakf800.c -o build/mining-endpoint-app-objects/keccakf800.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c $< -o build/mining-endpoint-app-objects/main.o
	$(CXX) $(CXXFLAGS) -Icore/include -Ithird_party/ravencoin-kawpow/src -c core/src/kawpow.cpp -o build/mining-endpoint-app-objects/onuros.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/ethash.cpp -o build/mining-endpoint-app-objects/ethash.o
	$(CXX) -std=c++17 -O2 -Ithird_party/ravencoin-kawpow/src -c third_party/ravencoin-kawpow/src/crypto/ethash/lib/ethash/progpow.cpp -o build/mining-endpoint-app-objects/progpow.o
	$(CXX) build/mining-endpoint-app-objects/*.o -o $@

build/%_test: tests/%_test.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/snapshot_trust_test: tests/snapshot_trust_test.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@ -lcrypto

sanitize:
	mkdir -p build
	@for source in tests/*_test.cpp; do \
	  name="$$(basename $$source)"; \
	  if [ "$$name" = "private_node_orchard_pipeline_test.cpp" ] || \
	     [ "$$name" = "snapshot_trust_test.cpp" ] || \
	     [ "$$name" = "tls_transport_test.cpp" ]; then continue; fi; \
	  output=build/$$(basename $$source .cpp)_sanitized; \
	  $(CXX) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	    -Icore/include $$source -o $$output || exit $$?; \
	  $$output || exit $$?; \
	done

clean:
	$(RM) $(addprefix build/,$(TESTS)) build/onuros-local-node build/onuros-stage7-network-node build/onuros-stage7-private-relay-node build/onuros-stage7-tls-probe build/onuros-mining-endpoint build/onuros-private-tps-benchmark build/onuros-stage7-bandwidth-benchmark
	$(RM) -r build/kawpow-objects
	$(RM) -r build/mining-endpoint-objects
	$(RM) -r build/mining-endpoint-app-objects
