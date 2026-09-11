#include "onuros/private_mempool.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace onuros;

namespace {
Hash256 value(std::uint64_t number) {
    Hash256 result{};
    for (std::size_t i = 0; i < 8U; ++i)
        result[result.size() - 1U - i] =
            static_cast<std::uint8_t>(number >> (i * 8U));
    return result;
}

class AdmissionBenchmarkVerifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit AdmissionBenchmarkVerifier(Hash256 anchor) : anchor_(anchor) {}
    VerifiedPrivateEffects verify(
            const TransactionEnvelope& transaction) const override {
        if (transaction.body.size() != 8U)
            return {PrivateProofError::malformed_encoding, {}, {}, {}, 0};
        std::uint64_t sequence = 0U;
        for (std::size_t i = 0; i < 8U; ++i)
            sequence |= static_cast<std::uint64_t>(transaction.body[i]) <<
                        (8U * i);
        return {PrivateProofError::none, anchor_, {value(sequence + 1U)},
                {value(sequence + 1'000'000U)}, 1};
    }
};
}

int main(int argc, char** argv) {
    std::size_t count = 100'000U;
    if (argc == 2) {
        const auto parsed = std::strtoull(argv[1], nullptr, 10);
        if (parsed == 0U || parsed > 1'000'000U) return 2;
        count = static_cast<std::size_t>(parsed);
    } else if (argc != 1) {
        return 2;
    }
    const auto genesis = value(1U);
    const auto root = value(2U);
    ShieldedState state(genesis, root);
    AdmissionBenchmarkVerifier verifier(root);
    PrivateMempool pool({count, count * 16U, count, 8U, 1U});
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        TransactionEnvelope transaction{2U, std::vector<std::uint8_t>(8U)};
        for (std::size_t byte = 0; byte < 8U; ++byte)
            transaction.body[byte] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(i) >> (8U * byte));
        if (!pool.add(transaction, state, verifier).accepted()) return 1;
    }
    const auto selected = pool.select(count, count * 16U, count);
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "Onuros Stage 6 admission-only benchmark\n"
              << "Transactions: " << selected.size() << '\n'
              << "Seconds: " << elapsed << '\n'
              << "Admission TPS: " << selected.size() / elapsed << '\n'
              << "Proof mode: scripted effects; this is not Orchard private TPS\n";
    return selected.size() == count ? 0 : 1;
}
