#include "onuros/state_snapshot.hpp"

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

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    const auto destination = std::filesystem::temp_directory_path() /
                             "onuros-state-snapshot-test.db";
    auto temporary = destination;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    std::filesystem::remove(temporary, ignored);

    const auto genesis = value(1U);
    const auto root = value(2U);
    const auto consensus = value(3U);
    const ChainEntry tip{genesis, {}, 0U, chain_work(5U),
                         chain_work(5U), 100U};
    const PruningPolicy policy{true, 10U, 20U};
    const auto checkpoint =
        make_pruning_checkpoint(policy, genesis, tip, root);
    check(checkpoint.accepted());

    const ShieldedState state(genesis, root);
    const auto content = shielded_store_detail::encode(state.snapshot());
    const auto manifest = make_state_snapshot_manifest(
        content, *checkpoint.checkpoint, consensus);
    const auto encoded = encode_state_snapshot_manifest(manifest);
    check(encoded == encode_state_snapshot_manifest(manifest));
    const auto decoded = decode_state_snapshot_manifest(encoded);
    check(decoded.has_value());
    check(decoded->checkpoint.active_tip == genesis);
    check(decoded->checkpoint.accumulated_work == tip.accumulated_work);
    check(decoded->content_size == content.size());
    check(decoded->content_hash == double_sha256(content));

    for (std::size_t index = 0U; index < encoded.size(); ++index) {
        auto corrupt = encoded;
        corrupt[index] ^= 1U;
        check(!decode_state_snapshot_manifest(corrupt));
    }
    auto truncated = encoded;
    truncated.pop_back();
    check(!decode_state_snapshot_manifest(truncated));
    auto trailing = encoded;
    trailing.push_back(0U);
    check(!decode_state_snapshot_manifest(trailing));

    {
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        output << "stable";
    }
    const auto stable = read_file(destination);
    auto wrong_id = state_snapshot_manifest_id(encoded);
    wrong_id.back() ^= 1U;
    check(import_authenticated_shielded_snapshot(
              destination, encoded, content, wrong_id,
              *checkpoint.checkpoint, consensus, 100U) ==
          StateSnapshotError::unauthenticated_manifest);
    check(read_file(destination) == stable);

    auto wrong_checkpoint = *checkpoint.checkpoint;
    wrong_checkpoint.tip_height = 1U;
    check(import_authenticated_shielded_snapshot(
              destination, encoded, content,
              state_snapshot_manifest_id(encoded), wrong_checkpoint,
              consensus, 100U) == StateSnapshotError::checkpoint_mismatch);
    check(import_authenticated_shielded_snapshot(
              destination, encoded, content,
              state_snapshot_manifest_id(encoded), *checkpoint.checkpoint,
              value(9U), 100U) ==
          StateSnapshotError::consensus_parameters_mismatch);

    auto corrupt_content = content;
    corrupt_content.back() ^= 1U;
    check(import_authenticated_shielded_snapshot(
              destination, encoded, corrupt_content,
              state_snapshot_manifest_id(encoded), *checkpoint.checkpoint,
              consensus, 100U) == StateSnapshotError::content_hash_mismatch);

    const ShieldedState wrong_state(genesis, value(8U));
    const auto wrong_content =
        shielded_store_detail::encode(wrong_state.snapshot());
    const auto wrong_manifest = encode_state_snapshot_manifest(
        make_state_snapshot_manifest(
            wrong_content, *checkpoint.checkpoint, consensus));
    check(import_authenticated_shielded_snapshot(
              destination, wrong_manifest, wrong_content,
              state_snapshot_manifest_id(wrong_manifest),
              *checkpoint.checkpoint, consensus, 100U) ==
          StateSnapshotError::invalid_shielded_state);
    check(read_file(destination) == stable);

    check(import_authenticated_shielded_snapshot(
              destination, encoded, content,
              state_snapshot_manifest_id(encoded), *checkpoint.checkpoint,
              consensus, 100U) == StateSnapshotError::none);
    check(read_file(destination) == content);

    PersistentShieldedState imported(genesis, root, 1U << 20U, 100U);
    check(imported.open(destination) == ShieldedStoreError::none);
    check(imported.state().tip() == genesis && imported.state().root() == root);

    std::filesystem::remove(destination, ignored);
    std::filesystem::remove(temporary, ignored);
    return 0;
}
