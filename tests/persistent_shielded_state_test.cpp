#include "onuros/persistent_shielded_state.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

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

    const auto stable_bytes = read_file(path);
    check(restarted.reorg({value(99U)}, {}) ==
          ShieldedStoreError::state_transition_failed);
    check(restarted.state().tip() == value(14U));
    check(read_file(path) == stable_bytes);

    PersistentShieldedState wrong_genesis(value(99U), genesis_root,
                                           1U << 20U, 100U);
    check(wrong_genesis.open(path) == ShieldedStoreError::wrong_genesis);

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
