#include "onuros/recursive_proof_manifest.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: onuros-stage7-recursive-proof-gate MANIFEST\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "recursive_proof_gate=ERROR reason=cannot_open_manifest\n";
        return 2;
    }
    const auto manifest = onuros::parse_recursive_proof_manifest(input);
    if (!manifest.accepted()) {
        std::cerr << "recursive_proof_gate=ERROR reason=" << manifest.error
                  << '\n';
        return 2;
    }
    const auto gate =
        onuros::evaluate_recursive_proof_gate(manifest.measurements);
    if (!gate.passed()) {
        std::cout << "recursive_proof_gate=FAIL canonical_block_bytes="
                  << gate.canonical_block_bytes << " failures=";
        for (std::size_t index = 0U; index < gate.failures.size(); ++index) {
            if (index != 0U) std::cout << ',';
            std::cout << onuros::recursive_proof_gate_failure_name(
                gate.failures[index]);
        }
        std::cout << '\n';
        return 1;
    }
    std::cout << "recursive_proof_gate=PASS transfers="
              << manifest.measurements.transfers
              << " canonical_block_bytes=" << gate.canonical_block_bytes
              << " prover_p99_ms=" << manifest.measurements.prover_p99_ms
              << " verifier_ms=" << manifest.measurements.verifier_ms
              << " backend=" << manifest.backend
              << " backend_commit=" << manifest.backend_commit << '\n';
    return 0;
}
