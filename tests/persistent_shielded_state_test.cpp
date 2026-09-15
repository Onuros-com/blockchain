#include "onuros/persistent_shielded_state.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

using namespace onuros;

void check_impl(bool condition, int line) {
    if (!condition) {
        std::cerr << "persistent shielded state check failed at line "
                  << line << '\n';
        std::abort();
    }
}

#define check(condition) check_impl((condition), __LINE__)

Hash256 value(std::uint8_t byte) {
    Hash256 result{};
    result.back() = byte;
    return result;
}

class Verifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit Verifier(Hash256 anchor) : anchor_(anchor) {}
    VerifiedPrivateEffects verify(
            const TransactionEnvelope& transaction) const override {
        if (transaction.body.empty())
            return {PrivateProofError::malformed_encoding, {}, {}, {}, 0};
        const auto selector = transaction.body[0];
        return {PrivateProofError::none, anchor_,
                {value(static_cast<std::uint8_t>(selector + 40U))},
                {value(static_cast<std::uint8_t>(selector + 80U))}, selector};
    }
};

PrivateAdmissionLimits limits() {
    return {1024U, 2U, 2U, 16U};
}

PrivateBlockAdmission::Prepared prepare(const ShieldedState& state,
                                        std::uint8_t selector) {
    const Verifier verifier(state.root());
    const auto result = PrivateBlockAdmission::prepare(
        state, state.tip(), {{1U, {selector}}}, verifier, limits());
    check(result.accepted());
    return *result.prepared;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> encode_legacy(const ShieldedSnapshot& snapshot) {
    std::vector<std::uint8_t> payload;
    detail::append_hash(payload, snapshot.genesis_block);
    detail::append_hash(payload, snapshot.genesis_root);
    detail::append_hash(payload, snapshot.tip_block);
    detail::append_hash(payload, snapshot.current_root);
    shielded_store_detail::append_hashes(payload, snapshot.nullifiers);
    shielded_store_detail::append_hashes(payload, snapshot.commitments);
    detail::append_u32(payload,
                       static_cast<std::uint32_t>(snapshot.history.size()));
    for (const auto& undo : snapshot.history) {
        detail::append_hash(payload, undo.block_id);
        detail::append_hash(payload, undo.parent_block);
        detail::append_hash(payload, undo.previous_root);
        detail::append_hash(payload, undo.resulting_root);
        shielded_store_detail::append_hashes(payload, undo.nullifiers);
        shielded_store_detail::append_hashes(payload, undo.commitments);
    }
    std::vector<std::uint8_t> bytes(
        shielded_store_detail::legacy_magic.begin(),
        shielded_store_detail::legacy_magic.end());
    detail::append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    detail::append_hash(bytes, double_sha256(bytes));
    return bytes;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      "onuros-shielded-state-test.db";
    auto temporary = path;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);

    const auto genesis = value(1U);
    const auto genesis_root = value(2U);
    const auto block_one = value(3U);
    const auto block_two = value(4U);
    const auto root_one = value(5U);
    const auto root_two = value(6U);

    PersistentShieldedState store(genesis, genesis_root, 1U << 20U, 100U);
    check(store.open(path) == ShieldedStoreError::none);
    const auto first = prepare(store.state(), 1U);
    check(store.connect(block_one, root_one, first) == ShieldedStoreError::none);
    const auto second = prepare(store.state(), 2U);
    check(store.connect(block_two, root_two, second) == ShieldedStoreError::none);
    check(store.state().height() == 2U && store.state().tip() == block_two);

    const auto legacy_bytes = encode_legacy(store.state().snapshot());
    const auto legacy = shielded_store_detail::decode(legacy_bytes, 100U);
    check(legacy && legacy->height == 2U &&
          legacy->history_base_height == 0U &&
          legacy->ordered_commitments.size() == 2U &&
          legacy->ordered_roots.size() == 3U);

    PersistentShieldedState restarted(genesis, genesis_root, 1U << 20U, 100U);
    check(restarted.open(path) == ShieldedStoreError::none);
    check(restarted.state().tip() == block_two &&
          restarted.state().root() == root_two &&
          restarted.state().spent_count() == 2U &&
          restarted.state().commitment_count() == 2U);

    ShieldedState fork(genesis, genesis_root);
    const auto fork_first = prepare(fork, 11U);
    check(fork.connect(value(13U), value(15U), fork_first) ==
          PrivateAdmissionError::none);
    const auto fork_second = prepare(fork, 12U);
    const std::vector<ShieldedConnect> connects{
        {value(13U), value(15U), fork_first},
        {value(14U), value(16U), fork_second}};
    check(restarted.reorg({block_two, block_one}, connects) ==
          ShieldedStoreError::none);
    check(restarted.state().tip() == value(14U) &&
          restarted.state().root() == value(16U) &&
          !restarted.state().spent(value(41U)) &&
          restarted.state().spent(value(51U)));

    PruningCheckpoint checkpoint;
    checkpoint.genesis = genesis;
    checkpoint.active_tip = value(14U);
    checkpoint.shielded_root = value(16U);
    checkpoint.tip_height = 2U;
    checkpoint.finality_depth = 1U;
    checkpoint.reorganization_window = 1U;
    checkpoint.prune_below_height = 2U;
    auto wrong_checkpoint = checkpoint;
    wrong_checkpoint.shielded_root = value(99U);
    check(restarted.retain_undo_history(wrong_checkpoint) ==
          ShieldedStoreError::checkpoint_mismatch);
    check(restarted.retain_undo_history(checkpoint) ==
          ShieldedStoreError::none);
    check(restarted.state().height() == 2U &&
          restarted.state().history().size() == 1U &&
          restarted.state().history_base_height() == 1U &&
          restarted.state().undo_retention_limit() == 1U);
    check(restarted.state().has_anchor(genesis_root) &&
          restarted.state().has_anchor(value(15U)) &&
          restarted.state().has_anchor(value(16U)));

    PersistentShieldedState retained_restart(
        genesis, genesis_root, 1U << 20U, 100U);
    check(retained_restart.open(path) == ShieldedStoreError::none);
    check(retained_restart.state().height() == 2U &&
          retained_restart.state().history().size() == 1U &&
          retained_restart.state().tip() == value(14U));
    check(retained_restart.disconnect(value(14U)) == ShieldedStoreError::none);
    check(retained_restart.disconnect(value(13U)) ==
          ShieldedStoreError::state_transition_failed);
    const auto restored_second = prepare(retained_restart.state(), 12U);
    check(retained_restart.connect(value(14U), value(16U), restored_second) ==
          ShieldedStoreError::none);

    const auto stable_bytes = read_file(path);
    check(retained_restart.reorg({value(99U)}, {}) ==
          ShieldedStoreError::state_transition_failed);
    check(retained_restart.state().tip() == value(14U));
    check(read_file(path) == stable_bytes);

    PersistentShieldedState wrong_genesis(value(99U), genesis_root,
                                           1U << 20U, 100U);
    check(wrong_genesis.open(path) == ShieldedStoreError::wrong_genesis);

    const auto seeded_path = std::filesystem::temp_directory_path() /
                             "onuros-shielded-seeded-test.db";
    std::filesystem::remove(seeded_path, ignored);
    const std::vector<Hash256> initial_commitments{value(70U), value(71U)};
    {
        PersistentShieldedState seeded(
            genesis, genesis_root, initial_commitments, 1U << 20U, 100U);
        check(seeded.open(seeded_path) == ShieldedStoreError::none);
        check(seeded.state().commitment_count() == 2U &&
              seeded.state().ordered_commitments() == initial_commitments);
    }
    {
        PersistentShieldedState seeded_restart(
            genesis, genesis_root, initial_commitments, 1U << 20U, 100U);
        check(seeded_restart.open(seeded_path) == ShieldedStoreError::none);
        PersistentShieldedState wrong_seed(
            genesis, genesis_root, {value(72U)}, 1U << 20U, 100U);
        check(wrong_seed.open(seeded_path) ==
              ShieldedStoreError::wrong_genesis);
    }
    std::filesystem::remove(seeded_path, ignored);

    auto corrupted = stable_bytes;
    corrupted[20U] ^= 1U;
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(corrupted.data()),
                     static_cast<std::streamsize>(corrupted.size()));
    }
    PersistentShieldedState rejects_corruption(genesis, genesis_root,
                                                1U << 20U, 100U);
    check(rejects_corruption.open(path) ==
          ShieldedStoreError::corrupt_database);

    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
    return 0;
}
