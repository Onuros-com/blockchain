#include "onuros/private_mempool.hpp"

#include <cstdlib>

namespace {
using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint32_t number) {
    Hash256 result{};
    for (std::size_t i = 0; i < 4U; ++i)
        result[result.size() - 1U - i] =
            static_cast<std::uint8_t>(number >> (i * 8U));
    return result;
}

TransactionEnvelope transaction(std::uint8_t selector,
                                std::uint8_t nullifier = 0U) {
    return {2U, {selector, nullifier == 0U ? selector : nullifier}};
}

class Verifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit Verifier(Hash256 anchor) : anchor_(anchor) {}
    VerifiedPrivateEffects verify(
            const TransactionEnvelope& candidate) const override {
        if (candidate.body.size() != 2U)
            return {PrivateProofError::malformed_encoding, {}, {}, {}, 0};
        const auto selector = candidate.body[0];
        return {PrivateProofError::none, anchor_,
                {value(100U + candidate.body[1])},
                {value(200U + selector)}, selector};
    }
};

PrivateMempoolLimits limits() {
    return {100U, 4096U, 100U, 64U, 2U};
}
}

int main() {
    const auto genesis = value(1U);
    const auto root = value(2U);
    ShieldedState state(genesis, root);
    Verifier verifier(root);
    PrivateMempool pool(limits());

    check(pool.add(transaction(1U), state, verifier).accepted());
    check(pool.add(transaction(1U), state, verifier).error ==
          PrivateMempoolError::duplicate_transaction);
    check(pool.add(transaction(2U, 1U), state, verifier).error ==
          PrivateMempoolError::conflicting_nullifier);
    check(pool.add(transaction(3U), state, verifier).accepted());
    check(pool.add(transaction(2U), state, verifier).accepted());
    check(pool.size() == 3U && pool.total_actions() == 3U);

    const auto selected = pool.select(2U, 4096U, 2U);
    check(selected.size() == 2U && selected[0].body[0] == 3U &&
          selected[1].body[0] == 2U);
    pool.remove_confirmed({transaction(3U)});
    check(pool.size() == 2U);

    const auto admitted = PrivateBlockAdmission::prepare(
        state, state.tip(), {transaction(1U)}, verifier,
        {64U, 2U, 2U, 10U});
    check(admitted.accepted());
    check(state.connect(value(3U), value(4U), *admitted.prepared) ==
          PrivateAdmissionError::none);
    Verifier advanced(value(4U));
    check(pool.revalidate(state, advanced) == 1U && pool.size() == 1U);

    ShieldedState node_a(genesis, root);
    ShieldedState node_b(genesis, root);
    Verifier common(root);
    PrivateMempool mempool_a(limits());
    PrivateMempool mempool_b(limits());
    for (std::uint8_t i = 10U; i < 20U; ++i) {
        check(mempool_a.add(transaction(i), node_a, common).accepted());
        check(mempool_b.add(transaction(i), node_b, common).accepted());
    }
    const auto block_a = mempool_a.select(10U, 4096U, 10U);
    const auto block_b = mempool_b.select(10U, 4096U, 10U);
    check(transaction_root(block_a) == transaction_root(block_b));
    const auto prepared_a = PrivateBlockAdmission::prepare(
        node_a, node_a.tip(), block_a, common, {64U, 2U, 2U, 20U});
    const auto prepared_b = PrivateBlockAdmission::prepare(
        node_b, node_b.tip(), block_b, common, {64U, 2U, 2U, 20U});
    check(prepared_a.accepted() && prepared_b.accepted());
    check(node_a.connect(value(30U), value(31U), *prepared_a.prepared) ==
          PrivateAdmissionError::none);
    check(node_b.connect(value(30U), value(31U), *prepared_b.prepared) ==
          PrivateAdmissionError::none);
    check(node_a.snapshot().nullifiers == node_b.snapshot().nullifiers &&
          node_a.snapshot().commitments == node_b.snapshot().commitments);

    auto tiny = limits();
    tiny.max_transactions = 1U;
    PrivateMempool bounded(tiny);
    check(bounded.add(transaction(30U), ShieldedState(genesis, root), common)
              .accepted());
    check(bounded.add(transaction(31U), ShieldedState(genesis, root), common)
              .error == PrivateMempoolError::pool_full);
    return 0;
}
