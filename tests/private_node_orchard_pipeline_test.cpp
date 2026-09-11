#include "onuros/orchard_ffi_backend.hpp"
#include "onuros/persistent_shielded_state.hpp"
#include "onuros/private_block.hpp"
#include "onuros/private_mempool.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace onuros;

#define check(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "pipeline check failed at line " << __LINE__         \
                      << ": " #condition << '\n';                              \
            std::abort();                                                      \
        }                                                                      \
    } while (false)

Hash256 value(std::uint8_t byte) {
    Hash256 result{};
    result.back() = byte;
    return result;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input));
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const Hash256& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(static_cast<bool>(output));
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output));
}

PrivateBundleLimits bundle_limits() {
    return {1U << 20U, 8U, 1U << 20U};
}

PrivateAdmissionLimits admission_limits() {
    return {1U << 20U, 8U, 8U, 32U};
}

PrivateMempoolLimits mempool_limits() {
    return {32U, 8U << 20U, 256U, 1U << 20U, 8U};
}

int write_digest(const std::filesystem::path& fixture,
                 const std::filesystem::path& output) {
    const TransactionEnvelope transaction{
        private_transaction_envelope_version, read_file(fixture)};
    const auto decoded = decode_private_transaction(transaction, bundle_limits());
    check(decoded.accepted());
    check(decoded.bundle->value_balance == 7);
    check(decoded.bundle->fee == 7);
    write_file(output, private_signature_digest(*decoded.bundle));
    return 0;
}

int run_pipeline(const std::filesystem::path& fixture) {
    const TransactionEnvelope transaction{
        private_transaction_envelope_version, read_file(fixture)};
    const auto decoded = decode_private_transaction(transaction, bundle_limits());
    check(decoded.accepted());
    check(decoded.bundle->value_balance == decoded.bundle->fee);

    OrchardFfiBackend backend;
    CanonicalPrivateTransactionVerifier verifier(backend, bundle_limits());
    const auto effects = verifier.verify(transaction);
    check(effects.error == PrivateProofError::none);
    check(effects.fee == 7);
    check(effects.anchor == decoded.bundle->anchor);

    const auto genesis = value(1U);
    ShieldedState state(genesis, effects.anchor);
    PrivateMempool pool(mempool_limits());
    check(pool.add(transaction, state, verifier).accepted());
    check(pool.add(transaction, state, verifier).error ==
          PrivateMempoolError::duplicate_transaction);
    check(pool.size() == 1U);

    auto bad_signature = *decoded.bundle;
    bad_signature.actions.front().spend_authorization.front() ^= 1U;
    const auto rejected_signature = pool.add(
        make_private_transaction(bad_signature), state, verifier);
    check(rejected_signature.error == PrivateMempoolError::verification_failed);
    check(rejected_signature.proof_error == PrivateProofError::invalid_signature);

    auto bad_proof = *decoded.bundle;
    bad_proof.proof[bad_proof.proof.size() / 2U] ^= 1U;
    const auto rejected_proof = pool.add(
        make_private_transaction(bad_proof), state, verifier);
    check(rejected_proof.error == PrivateMempoolError::verification_failed);
    check(rejected_proof.proof_error == PrivateProofError::invalid_proof);

    const auto selected = pool.select(8U, 8U << 20U, 32U);
    check(selected.size() == 1U);
    const auto admission = PrivateBlockAdmission::prepare(
        state, state.tip(), selected, verifier, admission_limits());
    check(admission.accepted());

    OrchardFfiRootCalculator roots;
    const auto resulting_root = roots.calculate(
        state.ordered_commitments(), admission.prepared->commitments());
    check(resulting_root.has_value());

    const auto team = value(10U);
    const auto ecosystem = value(11U);
    const auto miner = value(12U);
    PrivateRewardPolicy reward_policy(team, ecosystem);
    OnurosRewardPolicy economics;
    const auto amounts = economics.allocate(1U, admission.prepared->fees(), 0);
    const auto reward = make_private_reward_transaction(
        {amounts,
         amounts.miner == 0 ? Hash256{} : miner,
         amounts.team == 0 ? Hash256{} : team,
         amounts.ecosystem == 0 ? Hash256{} : ecosystem});

    Block block;
    block.header.version = 2U;
    block.header.height = 1U;
    block.header.previous = state.tip();
    block.header.shielded_root = *resulting_root;
    block.transactions = {reward, selected.front()};
    block.header.transactions_root = transaction_root(block.transactions);

    const auto prepared = PrivateBlockValidator::prepare(
        state, block, verifier, roots, reward_policy, admission_limits());
    check(prepared.accepted());

    auto wrong_root = block;
    wrong_root.header.shielded_root.back() ^= 1U;
    check(PrivateBlockValidator::prepare(
              state, wrong_root, verifier, roots, reward_policy,
              admission_limits()).error ==
          PrivateBlockError::invalid_shielded_root);

    const auto path = std::filesystem::temp_directory_path() /
                      "onuros-real-orchard-pipeline.db";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    const auto id = block_id(block.header);
    PersistentShieldedState persistent(genesis, effects.anchor,
                                        8U << 20U, 1024U);
    check(persistent.open(path) == ShieldedStoreError::none);
    check(persistent.connect(id, *resulting_root, *prepared.prepared) ==
          ShieldedStoreError::none);
    pool.remove_confirmed(selected);
    check(pool.size() == 0U);

    PersistentShieldedState restarted(genesis, effects.anchor,
                                       8U << 20U, 1024U);
    check(restarted.open(path) == ShieldedStoreError::none);
    check(restarted.state().tip() == id);
    check(restarted.state().root() == *resulting_root);
    check(restarted.state().spent_count() == effects.nullifiers.size());
    check(restarted.state().commitment_count() == effects.commitments.size());
    check(restarted.disconnect(id) == ShieldedStoreError::none);
    check(restarted.state().tip() == genesis);
    check(restarted.state().root() == effects.anchor);
    check(restarted.connect(id, *resulting_root, *prepared.prepared) ==
          ShieldedStoreError::none);
    check(restarted.state().tip() == id);

    std::filesystem::remove(path, ignored);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--write-digest")
        return write_digest(argv[2], argv[3]);
    if (argc == 3 && std::string(argv[1]) == "--run")
        return run_pipeline(argv[2]);
    return 2;
}
