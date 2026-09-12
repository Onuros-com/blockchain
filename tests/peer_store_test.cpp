#include "onuros/peer_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
PeerAddress address(std::uint8_t last, std::uint16_t port) {
    PeerAddress result;
    result.address = {127U, 0U, 0U, last};
    result.port = port;
    return result;
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        "onuros-stage7-peers.db";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    try {
        PersistentPeerStore store({3U, 10U, 40U});
        check(store.remember(address(1U, 7777U), 1U), "peer remembered");
        check(store.mark_success(address(2U, 7778U), 100U, 3U),
              "successful peer inserted");
        check(store.mark_failure(address(1U, 7777U), 100U) &&
              store.candidates(109U, 3U).size() == 1U,
              "failed peer observes retry delay");
        check(store.candidates(110U, 3U).size() == 2U &&
              store.candidates(110U, 3U).front().endpoint == address(2U, 7778U),
              "successful peers are preferred deterministically");
        check(store.mark_failure(address(1U, 7777U), 110U) &&
              store.candidates(129U, 3U).size() == 1U &&
              store.candidates(130U, 3U).size() == 2U,
              "retry delay applies bounded exponential backoff");
        check(!store.remember(PeerAddress{}, 1U), "unspecified address rejected");
        check(store.save(path), "peer store persisted atomically");

        PersistentPeerStore recovered({3U, 10U, 40U});
        check(recovered.load(path) && recovered.size() == 2U,
              "peer store recovered after restart");
        {
            std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(-1, std::ios::end);
            file.put('\0');
        }
        PersistentPeerStore corrupt({3U, 10U, 40U});
        check(!corrupt.load(path) && corrupt.size() == 0U,
              "corrupt peer database rejected atomically");
        std::cout << checks << " peer-store checks passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove(path, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove(path, ignored);
}
