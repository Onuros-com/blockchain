#include "onuros/private_node_commit.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace onuros;

unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Hash256 value(std::uint8_t byte) {
    Hash256 result{};
    result.back() = byte;
    return result;
}

Hash256 test_pow(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

class Verifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit Verifier(Hash256 anchor) : anchor_(anchor) {}
    VerifiedPrivateEffects verify(
            const TransactionEnvelope& transaction) const override {
        if (transaction.body.size() != 1U || transaction.body.front() == 0U)
            return {PrivateProofError::invalid_proof, {}, {}, {}, 0};
        const auto byte = transaction.body.front();
        return {PrivateProofError::none, anchor_, {value(byte)},
                {value(static_cast<std::uint8_t>(byte + 20U))}, 7};
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

LocalNodeParameters parameters() {
    const auto pow_limit = decode_compact_target(0x207fffffU);
    check(pow_limit.has_value(), "test proof-of-work limit decodes");
    LocalNodeParameters result;
    result.validation_limits = {2U, 2U, 4096U, 8U, 1024U};
    result.decode_limits = {4096U, 8U, 1024U};
    result.difficulty.target_block_seconds = 60U;
    result.difficulty.retarget_interval = 60U;
    result.difficulty.adjustment_clamp_factor = 4U;
    result.difficulty.proof_of_work_limit = *pow_limit;
    result.max_future_seconds = 120U;
    result.max_database_bytes = 1U << 20U;
    return result;
}

void solve(Block& block, const Target256& pow_limit) {
    while (!hash_meets_compact_target(test_pow(block.header),
                                      block.header.compact_target, pow_limit))
        ++block.header.nonce;
}

Block add_genesis(LocalNode& node, const Target256& pow_limit,
                  const Hash256& initial_root) {
    auto candidate = node.make_candidate({{1U, {1U}}}, 100U);
    check(candidate.has_value(), "genesis candidate created");
    candidate->header.shielded_root = initial_root;
    solve(*candidate, pow_limit);
    check(node.submit(*candidate, 100U).error == LocalNodeError::none,
          "genesis committed");
    return *candidate;
}

Block private_candidate(LocalNode& node, const ShieldedState& state,
        const PrivateRewardPolicy& rewards, const Verifier& verifier,
        const TestRoot& roots, const Hash256& team,
        const Hash256& ecosystem, std::uint8_t byte,
        std::uint64_t timestamp, const Target256& pow_limit) {
    const TransactionEnvelope private_transaction{
        private_transaction_envelope_version, {byte}};
    const auto admission = PrivateBlockAdmission::prepare(
        state, state.tip(), {private_transaction}, verifier,
        {1024U, 4U, 4U, 8U});
    check(admission.accepted(), "candidate private effects prepared");
    OnurosRewardPolicy economics;
    const auto amounts = economics.allocate(
        static_cast<Height>(state.height() + 1U),
        admission.prepared->fees(), 0);
    const auto reward = make_private_reward_transaction({
        amounts, amounts.miner == 0 ? Hash256{} : value(12U),
        amounts.team == 0 ? Hash256{} : team,
        amounts.ecosystem == 0 ? Hash256{} : ecosystem});
    check(rewards.validate(static_cast<Height>(state.height() + 1U),
                           *admission.prepared, reward) ==
              PrivateRewardError::none,
          "candidate reward validates");
    auto block = node.make_candidate({reward, private_transaction}, timestamp);
    check(block.has_value(), "private block candidate created");
    block->header.shielded_root = *roots.calculate(
        state.ordered_commitments(), admission.prepared->commitments());
    solve(*block, pow_limit);
    return *block;
}

struct Paths {
    std::filesystem::path blocks;
    std::filesystem::path shielded;
    std::filesystem::path journal;
};

Paths paths(const std::string& name) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("onuros-private-commit-" + name);
    return {base.string() + "-blocks.db", base.string() + "-shielded.db",
            base.string() + "-journal.db"};
}

void clean(const Paths& files) {
    std::error_code ignored;
    for (const auto& path : {files.blocks, files.shielded, files.journal}) {
        std::filesystem::remove(path, ignored);
        auto temporary = path;
        temporary += ".tmp";
        std::filesystem::remove(temporary, ignored);
    }
}

void normal_commit() {
    const auto files = paths("normal");
    clean(files);
    const auto config = parameters();
    const auto root = value(9U);
    const auto team = value(10U);
    const auto ecosystem = value(11U);
    LocalNode node(config, test_pow);
    check(node.open(files.blocks).error == LocalNodeError::none,
          "normal block store opens");
    const auto genesis = add_genesis(
        node, config.difficulty.proof_of_work_limit, root);
    PersistentShieldedState state(
        block_id(genesis.header), root, 1U << 20U, 100U);
    check(state.open(files.shielded) == ShieldedStoreError::none,
          "normal shielded store opens");
    Verifier verifier(root);
    TestRoot roots;
    PrivateRewardPolicy rewards(team, ecosystem);
    PrivateNodeCommitCoordinator coordinator(
        node, state, verifier, roots, rewards, {1024U, 4U, 4U, 8U},
        files.journal, config.decode_limits, 8192U);
    const auto block = private_candidate(
        node, state.state(), rewards, verifier, roots, team, ecosystem,
        2U, 160U, config.difficulty.proof_of_work_limit);
    check(coordinator.submit(block, 160U).accepted(),
          "coordinated commit succeeds");
    check(node.active_state().tip() == block_id(block.header) &&
              state.state().tip() == block_id(block.header),
          "both durable states advance to one tip");
    check(!std::filesystem::exists(files.journal),
          "completed commit clears journal");
    clean(files);
}

void recover_uncommitted_intent() {
    const auto files = paths("uncommitted");
    clean(files);
    const auto config = parameters();
    const auto root = value(19U);
    const auto team = value(20U);
    const auto ecosystem = value(21U);
    LocalNode node(config, test_pow);
    check(node.open(files.blocks).error == LocalNodeError::none,
          "uncommitted block store opens");
    const auto genesis = add_genesis(
        node, config.difficulty.proof_of_work_limit, root);
    PersistentShieldedState state(
        block_id(genesis.header), root, 1U << 20U, 100U);
    check(state.open(files.shielded) == ShieldedStoreError::none,
          "uncommitted shielded store opens");
    Verifier verifier(root);
    TestRoot roots;
    PrivateRewardPolicy rewards(team, ecosystem);
    PrivateNodeCommitCoordinator coordinator(
        node, state, verifier, roots, rewards, {1024U, 4U, 4U, 8U},
        files.journal, config.decode_limits, 8192U);
    const auto block = private_candidate(
        node, state.state(), rewards, verifier, roots, team, ecosystem,
        3U, 160U, config.difficulty.proof_of_work_limit);
    check(coordinator.journal().write(block, 160U) ==
              PrivateCommitJournalError::none,
          "uncommitted intent written");
    const auto recovered = coordinator.recover();
    check(recovered.accepted() && recovered.recovery_action ==
              PrivateRecoveryAction::discarded_uncommitted_intent,
          "recovery discards intent without durable block");
    check(node.active_state().tip() == block_id(genesis.header) &&
              state.state().tip() == block_id(genesis.header),
          "discarded intent changes neither state");
    clean(files);
}

void recover_durable_block() {
    const auto files = paths("durable-block");
    clean(files);
    const auto config = parameters();
    const auto root = value(29U);
    const auto team = value(30U);
    const auto ecosystem = value(31U);
    Block committed;
    Hash256 genesis_id{};
    {
        LocalNode node(config, test_pow);
        check(node.open(files.blocks).error == LocalNodeError::none,
              "recovery block store opens");
        const auto genesis = add_genesis(
            node, config.difficulty.proof_of_work_limit, root);
        genesis_id = block_id(genesis.header);
        PersistentShieldedState state(genesis_id, root, 1U << 20U, 100U);
        check(state.open(files.shielded) == ShieldedStoreError::none,
              "recovery shielded store opens");
        Verifier verifier(root);
        TestRoot roots;
        PrivateRewardPolicy rewards(team, ecosystem);
        PrivateNodeCommitCoordinator coordinator(
            node, state, verifier, roots, rewards, {1024U, 4U, 4U, 8U},
            files.journal, config.decode_limits, 8192U);
        committed = private_candidate(
            node, state.state(), rewards, verifier, roots, team, ecosystem,
            4U, 160U, config.difficulty.proof_of_work_limit);
        check(coordinator.journal().write(committed, 160U) ==
                  PrivateCommitJournalError::none,
              "durable block intent written");
        check(node.submit(committed, 160U).error == LocalNodeError::none,
              "block append completed before simulated crash");
    }
    LocalNode restarted_node(config, test_pow);
    check(restarted_node.open(files.blocks).error == LocalNodeError::none,
          "block store restarts after interrupted commit");
    PersistentShieldedState restarted_state(
        genesis_id, root, 1U << 20U, 100U);
    check(restarted_state.open(files.shielded) == ShieldedStoreError::none,
          "shielded store restarts behind block tip");
    Verifier verifier(root);
    TestRoot roots;
    PrivateRewardPolicy rewards(team, ecosystem);
    PrivateNodeCommitCoordinator restarted(
        restarted_node, restarted_state, verifier, roots, rewards,
        {1024U, 4U, 4U, 8U}, files.journal, config.decode_limits, 8192U);
    const auto recovered = restarted.recover();
    check(recovered.accepted() && recovered.recovery_action ==
              PrivateRecoveryAction::completed_shielded_commit,
          "recovery completes shielded state from durable block");
    check(restarted_node.active_state().tip() == block_id(committed.header) &&
              restarted_state.state().tip() == block_id(committed.header),
          "restart reconciles both durable tips");
    check(!std::filesystem::exists(files.journal),
          "reconciled commit clears journal");
    clean(files);
}

void reject_corrupt_journal() {
    const auto files = paths("corrupt");
    clean(files);
    const auto config = parameters();
    const auto root = value(39U);
    const auto team = value(40U);
    const auto ecosystem = value(41U);
    LocalNode node(config, test_pow);
    check(node.open(files.blocks).error == LocalNodeError::none,
          "corrupt-journal block store opens");
    const auto genesis = add_genesis(
        node, config.difficulty.proof_of_work_limit, root);
    PersistentShieldedState state(
        block_id(genesis.header), root, 1U << 20U, 100U);
    check(state.open(files.shielded) == ShieldedStoreError::none,
          "corrupt-journal shielded store opens");
    {
        std::ofstream output(files.journal, std::ios::binary);
        output << "damaged";
    }
    Verifier verifier(root);
    TestRoot roots;
    PrivateRewardPolicy rewards(team, ecosystem);
    PrivateNodeCommitCoordinator coordinator(
        node, state, verifier, roots, rewards, {1024U, 4U, 4U, 8U},
        files.journal, config.decode_limits, 8192U);
    const auto recovered = coordinator.recover();
    check(recovered.error == PrivateCommitError::journal_error &&
              recovered.journal_error == PrivateCommitJournalError::corrupt,
          "corrupt recovery journal fails closed");
    check(std::filesystem::exists(files.journal),
          "corrupt journal is retained for operator inspection");
    clean(files);
}
}

int main() {
    try {
        normal_commit();
        recover_uncommitted_intent();
        recover_durable_block();
        reject_corrupt_journal();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
