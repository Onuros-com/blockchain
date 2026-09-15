#include "onuros/persistent_shielded_state.hpp"
#include "onuros/private_block.hpp"

#include <cstdlib>
#include <filesystem>

namespace {
using namespace onuros;

void check(bool condition) { if (!condition) std::abort(); }
Hash256 value(std::uint8_t byte) { Hash256 h{}; h.back() = byte; return h; }

class Verifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit Verifier(Hash256 anchor) : anchor_(anchor) {}
    VerifiedPrivateEffects verify(const TransactionEnvelope& tx) const override {
        if (tx.body.size() != 1U || tx.body[0] == 0U)
            return {PrivateProofError::invalid_proof, {}, {}, {}, 0};
        return {PrivateProofError::none, anchor_, {value(tx.body[0])},
                {value(static_cast<std::uint8_t>(tx.body[0] + 20U))}, 7};
    }
};

class TestRoot final : public ShieldedRootCalculator {
public:
    std::optional<Hash256> calculate(
            const std::vector<Hash256>& active,
            const std::vector<Hash256>& added) const override {
        std::vector<std::uint8_t> bytes;
        for (const auto& item : active) detail::append_hash(bytes, item);
        for (const auto& item : added) detail::append_hash(bytes, item);
        return double_sha256(bytes);
    }
};

Block candidate(const ShieldedState& state, Height height,
                const PrivateRewardPolicy& policy, const Verifier& verifier,
                const TestRoot& roots, Hash256 team, Hash256 ecosystem) {
    const TransactionEnvelope private_tx{3U, {2U}};
    const auto admission = PrivateBlockAdmission::prepare(
        state, state.tip(), {private_tx}, verifier, {1024U, 4U, 4U, 8U});
    check(admission.accepted());
    OnurosRewardPolicy economics;
    const auto amounts = economics.allocate(
        height, admission.prepared->fees(), 0);
    const PrivateRewardClaim claim{
        amounts, amounts.miner == 0 ? Hash256{} : value(12U),
        amounts.team == 0 ? Hash256{} : team,
        amounts.ecosystem == 0 ? Hash256{} : ecosystem};
    const auto reward = make_private_reward_transaction(claim);
    Block block;
    block.header.version = 2U;
    block.header.height = height;
    block.header.previous = state.tip();
    block.transactions = {reward, private_tx};
    block.header.transactions_root = transaction_root(block.transactions);
    block.header.shielded_root = *roots.calculate(
        state.ordered_commitments(), admission.prepared->commitments());
    check(policy.validate(height, *admission.prepared, reward) ==
          PrivateRewardError::none);
    return block;
}
}

int main() {
    const auto genesis = value(1U);
    const auto initial_root = value(9U);
    const auto team = value(10U);
    const auto ecosystem = value(11U);
    ShieldedState state(genesis, initial_root);
    Verifier verifier(initial_root);
    TestRoot roots;
    PrivateRewardPolicy rewards(team, ecosystem);
    const PrivateAdmissionLimits limits{1024U, 4U, 4U, 8U};
    auto block = candidate(state, 1U, rewards, verifier, roots, team, ecosystem);
    auto accepted = PrivateBlockValidator::prepare(
        state, block, verifier, roots, rewards, limits);
    check(accepted.accepted());

    const auto path = std::filesystem::temp_directory_path() /
                      "onuros-private-block-state.db";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    PersistentShieldedState persistent(genesis, initial_root, 1U << 20U, 100U);
    check(persistent.open(path) == ShieldedStoreError::none);
    check(persistent.connect(block_id(block.header), block.header.shielded_root,
                             *accepted.prepared) == ShieldedStoreError::none);
    check(persistent.state().root() == block.header.shielded_root);

    auto tampered = block;
    tampered.header.shielded_root = value(99U);
    check(PrivateBlockValidator::prepare(state, tampered, verifier, roots,
                                         rewards, limits).error ==
          PrivateBlockError::invalid_shielded_root);
    tampered = block;
    ++tampered.transactions.front().body[8U];
    tampered.header.transactions_root = transaction_root(tampered.transactions);
    auto bad_reward = PrivateBlockValidator::prepare(
        state, tampered, verifier, roots, rewards, limits);
    check(bad_reward.error == PrivateBlockError::invalid_reward);
    tampered = block;
    tampered.transactions[1].body[0] = 0U;
    tampered.header.transactions_root = transaction_root(tampered.transactions);
    auto bad_proof = PrivateBlockValidator::prepare(
        state, tampered, verifier, roots, rewards, limits);
    check(bad_proof.error == PrivateBlockError::admission_failed &&
          bad_proof.proof_error == PrivateProofError::invalid_proof);
    tampered = block;
    tampered.transactions.erase(tampered.transactions.begin());
    tampered.header.transactions_root = transaction_root(tampered.transactions);
    check(PrivateBlockValidator::prepare(state, tampered, verifier, roots,
                                         rewards, limits).error ==
          PrivateBlockError::malformed_reward);
    tampered = block;
    tampered.header.transactions_root = {};
    check(PrivateBlockValidator::prepare(state, tampered, verifier, roots,
                                         rewards, limits).error ==
          PrivateBlockError::invalid_transaction_root);
    std::filesystem::remove(path, ignored);
    return 0;
}
