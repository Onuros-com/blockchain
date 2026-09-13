#include "onuros/recursive_proof_manifest.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

std::string manifest() {
    const std::string hash(64U, 'a');
    return
        "format=onuros-stage7-recursive-proof-v1\n"
        "backend=example-recursive-backend\n"
        "backend_commit=" + hash + "\n"
        "proof_hash_run_1=" + hash + "\n"
        "proof_hash_run_2=" + hash + "\n"
        "raw_log_sha256=" + hash + "\n"
        "transfers=6000\n"
        "verified_onp2_transfers=6000\n"
        "effect_payload_bytes=3894000\n"
        "proof_bytes=1024\n"
        "block_overhead_bytes=4096\n"
        "prover_p99_ms=44999\n"
        "verifier_ms=59999\n"
        "propagation_p99_ms=14999\n"
        "peak_vram_mib=12288\n"
        "binds_parent_and_pow_block=true\n"
        "binds_ordered_effects_and_authorizations=true\n"
        "binds_old_and_new_state_roots=true\n"
        "binds_nullifiers_values_fees_and_reward=true\n"
        "rejects_corrupted_proof=true\n"
        "rejects_withheld_proof=true\n"
        "rejects_malformed_proof=true\n"
        "recovers_after_interruption=true\n";
}

RecursiveProofManifestResult parse(const std::string& value) {
    std::istringstream input(value);
    return parse_recursive_proof_manifest(input);
}

void replace(std::string& value, const std::string& before,
             const std::string& after) {
    const auto position = value.find(before);
    check(position != std::string::npos);
    value.replace(position, before.size(), after);
}

} // namespace

int main() {
    const auto accepted = parse(manifest());
    check(accepted.accepted());
    check(evaluate_recursive_proof_gate(accepted.measurements).passed());

    auto candidate = manifest();
    candidate += "unknown=value\n";
    check(!parse(candidate).accepted());

    candidate = manifest();
    candidate += "transfers=6000\n";
    check(!parse(candidate).accepted());

    candidate = manifest();
    replace(candidate, "proof_bytes=1024\n", "");
    check(!parse(candidate).accepted());

    candidate = manifest();
    replace(candidate, "transfers=6000", "transfers=+6000");
    check(!parse(candidate).accepted());

    candidate = manifest();
    replace(candidate, "rejects_corrupted_proof=true",
            "rejects_corrupted_proof=TRUE");
    check(!parse(candidate).accepted());

    candidate = manifest();
    replace(candidate, std::string(64U, 'a'), std::string(64U, 'A'));
    check(!parse(candidate).accepted());

    candidate = manifest();
    replace(candidate, "backend=example-recursive-backend", "backend=none");
    const auto no_backend = parse(candidate);
    check(no_backend.accepted());
    check(!evaluate_recursive_proof_gate(no_backend.measurements).passed());

    candidate = manifest();
    const std::string different_hash(64U, 'b');
    replace(candidate, "proof_hash_run_2=" + std::string(64U, 'a'),
            "proof_hash_run_2=" + different_hash);
    const auto nondeterministic = parse(candidate);
    check(nondeterministic.accepted());
    check(!nondeterministic.measurements.deterministic);
    check(!evaluate_recursive_proof_gate(nondeterministic.measurements).passed());

    candidate = manifest();
    replace(candidate, "format=onuros-stage7-recursive-proof-v1",
            "format=onuros-stage7-recursive-proof-v2");
    check(!parse(candidate).accepted());

    candidate = "bad-line\n";
    check(!parse(candidate).accepted());
    return 0;
}
