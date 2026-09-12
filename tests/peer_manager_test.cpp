#include "onuros/peer_manager.hpp"

#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
}

int main() {
    try {
        PeerManagerLimits limits;
        limits.maximum_connections = 3U;
        limits.maximum_connections_per_address = 2U;
        limits.ban_seconds = 60U;
        limits.handshake_timeout_seconds = 10U;
        limits.idle_timeout_seconds = 20U;
        PeerManager manager(limits);
        check(manager.admit("10.0.0.1", 100U) == PeerAdmissionError::none &&
              manager.admit("10.0.0.1", 100U) == PeerAdmissionError::none,
              "per-address allowance admitted");
        check(manager.admit("10.0.0.1", 100U) ==
                  PeerAdmissionError::address_limit,
              "per-address connection limit enforced");
        check(manager.admit("10.0.0.2", 100U) == PeerAdmissionError::none &&
              manager.admit("10.0.0.3", 100U) == PeerAdmissionError::total_limit,
              "global peer limit enforced");
        manager.release("10.0.0.1");
        check(manager.connection_count() == 2U &&
              manager.admit("10.0.0.3", 100U) == PeerAdmissionError::none,
              "disconnect releases global and address capacity");
        manager.ban("10.0.0.4", 100U);
        check(manager.admit("10.0.0.4", 159U) == PeerAdmissionError::banned,
              "temporary ban blocks admission");
        manager.prune_expired_bans(160U);
        manager.release("10.0.0.1");
        check(manager.admit("10.0.0.4", 160U) == PeerAdmissionError::none,
              "expired ban can be pruned");
        check(manager.admit("", 160U) == PeerAdmissionError::invalid_address,
              "empty peer address rejected");

        PeerDeadlineTracker handshake(limits, 100U);
        check(handshake.timeout(109U) == PeerTimeout::none &&
              handshake.timeout(110U) == PeerTimeout::handshake,
              "slow handshake reaches deadline");
        PeerDeadlineTracker active(limits, 100U);
        active.mark_handshake_complete(105U);
        active.mark_activity(110U);
        check(active.timeout(129U) == PeerTimeout::none &&
              active.timeout(130U) == PeerTimeout::idle,
              "idle established peer reaches deadline");

        std::cout << checks << " peer-manager checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
