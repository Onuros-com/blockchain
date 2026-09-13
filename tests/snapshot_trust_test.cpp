#include "onuros/snapshot_trust.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
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

struct KeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};

struct ContextDeleter {
    void operator()(EVP_PKEY_CTX* context) const noexcept {
        EVP_PKEY_CTX_free(context);
    }
    void operator()(EVP_MD_CTX* context) const noexcept {
        EVP_MD_CTX_free(context);
    }
};

using Key = std::unique_ptr<EVP_PKEY, KeyDeleter>;

Key generate_key() {
    std::unique_ptr<EVP_PKEY_CTX, ContextDeleter> context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
    check(context && EVP_PKEY_keygen_init(context.get()) == 1);
    EVP_PKEY* raw = nullptr;
    check(EVP_PKEY_keygen(context.get(), &raw) == 1 && raw != nullptr);
    return Key(raw);
}

std::array<std::uint8_t, 32> public_key(EVP_PKEY* key) {
    std::array<std::uint8_t, 32> output{};
    std::size_t size = output.size();
    check(EVP_PKEY_get_raw_public_key(key, output.data(), &size) == 1 &&
          size == output.size());
    return output;
}

SnapshotSignature sign(std::uint32_t identifier, EVP_PKEY* key,
                       const std::vector<std::uint8_t>& message) {
    std::unique_ptr<EVP_MD_CTX, ContextDeleter> context(EVP_MD_CTX_new());
    check(context && EVP_DigestSignInit(context.get(), nullptr, nullptr,
                                        nullptr, key) == 1);
    SnapshotSignature result;
    result.signer_identifier = identifier;
    std::size_t size = result.signature.size();
    check(EVP_DigestSign(context.get(), result.signature.data(), &size,
                         message.data(), message.size()) == 1 &&
          size == result.signature.size());
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
        const auto marker = transaction.body.front();
        return {PrivateProofError::none, anchor_,
                {value(static_cast<std::uint8_t>(marker + 40U))},
                {value(static_cast<std::uint8_t>(marker + 80U))}, marker};
    }
};

PrivateBlockAdmission::Prepared prepare(const ShieldedState& state,
                                        std::uint8_t marker) {
    const Verifier verifier(state.root());
    const auto result = PrivateBlockAdmission::prepare(
        state, state.tip(), {{1U, {marker}}}, verifier,
        {1024U, 2U, 2U, 16U});
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
    const auto destination = std::filesystem::temp_directory_path() /
                             "onuros-threshold-snapshot-test.db";
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    std::filesystem::remove(destination.string() + ".tmp", ignored);

    const auto genesis = value(1U);
    const auto genesis_root = value(2U);
    const auto block_one = value(3U);
    const auto block_two = value(4U);
    const auto root_one = value(5U);
    const auto root_two = value(6U);
    const auto consensus = value(7U);

    ShieldedState state(genesis, genesis_root);
    check(state.connect(block_one, root_one, prepare(state, 1U)) ==
          PrivateAdmissionError::none);
    check(state.connect(block_two, root_two, prepare(state, 2U)) ==
          PrivateAdmissionError::none);
    const auto content = shielded_store_detail::encode(state.snapshot());
    const ChainEntry tip{block_two, block_one, 2U, chain_work(5U),
                         chain_work(15U), 220U};
    const PruningPolicy pruning{true, 10U, 20U};
    const auto checkpoint = make_pruning_checkpoint(
        pruning, genesis, tip, root_two);
    check(checkpoint.accepted());
    const auto manifest = encode_state_snapshot_manifest(
        make_state_snapshot_manifest(content, *checkpoint.checkpoint,
                                     consensus));
    const auto message = snapshot_signature_message(manifest);

    auto first = generate_key();
    auto offline = generate_key();
    auto third = generate_key();
    const SnapshotThresholdPolicy policy{
        2U, true,
        {{1U, public_key(first.get()), false},
         {2U, public_key(offline.get()), true},
         {3U, public_key(third.get()), false}}};
    const auto first_signature = sign(1U, first.get(), message);
    const auto offline_signature = sign(2U, offline.get(), message);
    const auto third_signature = sign(3U, third.get(), message);

    check(verify_snapshot_threshold(
              manifest, {first_signature}, policy).error ==
          SnapshotTrustError::threshold_not_met);
    check(verify_snapshot_threshold(
              manifest, {first_signature, third_signature}, policy).error ==
          SnapshotTrustError::offline_security_signature_missing);
    check(verify_snapshot_threshold(
              manifest, {first_signature, first_signature}, policy).error ==
          SnapshotTrustError::duplicate_signer);
    auto unknown = third_signature;
    unknown.signer_identifier = 99U;
    check(verify_snapshot_threshold(
              manifest, {first_signature, unknown}, policy).error ==
          SnapshotTrustError::unknown_signer);
    auto invalid = offline_signature;
    invalid.signature.back() ^= 1U;
    check(verify_snapshot_threshold(
              manifest, {first_signature, invalid}, policy).error ==
          SnapshotTrustError::invalid_signature);

    {
        std::ofstream existing(destination, std::ios::binary | std::ios::trunc);
        existing << "stable";
    }
    const auto stable = read_file(destination);
    const auto rejected = import_threshold_authenticated_snapshot(
        destination, manifest, content, {first_signature}, policy,
        *checkpoint.checkpoint, consensus, 100U);
    check(rejected.trust_error == SnapshotTrustError::threshold_not_met &&
          read_file(destination) == stable);

    const auto imported = import_threshold_authenticated_snapshot(
        destination, manifest, content,
        {first_signature, offline_signature}, policy,
        *checkpoint.checkpoint, consensus, 100U);
    check(imported.imported());
    PersistentShieldedState late_node(
        genesis, genesis_root, 1U << 20U, 100U);
    check(late_node.open(destination) == ShieldedStoreError::none &&
          late_node.state().height() == 2U &&
          late_node.state().tip() == block_two &&
          late_node.state().root() == root_two);

    const auto block_three = value(8U);
    const auto root_three = value(9U);
    check(late_node.connect(
              block_three, root_three, prepare(late_node.state(), 3U)) ==
              ShieldedStoreError::none &&
          late_node.state().height() == 3U &&
          late_node.state().tip() == block_three &&
          late_node.state().root() == root_three);

    std::filesystem::remove(destination, ignored);
    std::filesystem::remove(destination.string() + ".tmp", ignored);
    return 0;
}
