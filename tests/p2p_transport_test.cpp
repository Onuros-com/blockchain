#include "onuros/p2p_transport.hpp"
#include "onuros/stage7_relay.hpp"
#include "loopback_test.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Hash256 value(std::uint8_t byte) { Hash256 hash{}; hash.back() = byte; return hash; }
bool send_all(TcpConnection& connection, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0U;
    for (unsigned attempt = 0U; attempt < 2000U && offset < bytes.size(); ++attempt) {
        const auto result = connection.send_some(bytes.data() + offset,
                                                  bytes.size() - offset);
        if (result.status == SocketIoStatus::ok) offset += result.bytes;
        else if (result.status != SocketIoStatus::would_block) return false;
        if (result.status == SocketIoStatus::would_block)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return offset == bytes.size();
}
FrameStreamResult receive_next(TcpConnection& connection,
                               FrameStreamDecoder& stream) {
    std::array<std::uint8_t, 17U> receive_buffer{};
    for (unsigned attempt = 0U; attempt < 4000U; ++attempt) {
        auto parsed = stream.next();
        if (parsed.status != FrameStreamStatus::incomplete) return parsed;
        const auto received = connection.receive_some(
            receive_buffer.data(), receive_buffer.size());
        if (received.status == SocketIoStatus::ok) {
            if (!stream.feed(receive_buffer.data(), received.bytes))
                return {FrameStreamStatus::failed,
                        P2pFrameError::oversized_payload, {}};
        } else if (received.status != SocketIoStatus::would_block) {
            return {FrameStreamStatus::failed, P2pFrameError::truncated, {}};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}
}

int main() {
    try {
        SocketRuntime runtime;
        check(runtime.ready(), "socket runtime initializes");
        auto pair = test::connect_loopback_pair();
        check(pair && pair->listener.valid() && pair->listener.port() != 0U,
              "nonblocking loopback listener opens");
        check(pair->client.valid(), "real TCP client connects");
        check(pair->server.valid(), "real TCP listener accepts peer");
        auto& client = pair->client;
        auto& server = pair->server;

        HelloMessage hello;
        hello.chain_id = value(1U);
        hello.genesis_hash = value(2U);
        hello.services = p2p_service_full_node | p2p_service_compact_relay;
        hello.node_nonce = 99U;
        const P2pFrame frame{stage7_protocol_version, P2pMessageType::hello,
                             1U, encode_hello(hello)};
        const auto wire = encode_p2p_frame(frame);
        check(send_all(client, wire), "framed hello sent over real socket");

        FrameStreamDecoder stream({}, 512U * 1024U);
        const auto parsed = receive_next(server, stream);
        check(parsed.status == FrameStreamStatus::frame_ready &&
              parsed.frame.type == P2pMessageType::hello,
              "fragmented real TCP frame reconstructed");

        HandshakePolicy policy;
        policy.chain_id = hello.chain_id;
        policy.genesis_hash = hello.genesis_hash;
        policy.local_nonce = 100U;
        policy.required_services = p2p_service_compact_relay;
        PeerSession session(policy);
        check(session.receive(parsed.frame, false) == PeerSessionError::none &&
              session.state() == PeerSessionState::established,
              "live peer handshake establishes session");
        check(session.receive(parsed.frame, false) ==
                  PeerSessionError::duplicate_hello &&
              session.state() == PeerSessionState::closed,
              "duplicate hello closes established session");

        Block block;
        block.header.version = 7U;
        block.header.height = 1U;
        block.transactions = {{1U, {1U, 2U}}, {1U, {3U, 4U}}};
        block.header.transactions_root = transaction_root(block.transactions);
        std::vector<Hash256> inventory;
        for (const auto& transaction : block.transactions)
            inventory.push_back(transaction_id(transaction));
        const auto inventory_wire = encode_p2p_frame({
            stage7_protocol_version, P2pMessageType::transaction_inventory,
            2U, encode_inventory(inventory)});
        const auto announcement = make_compact_block_announcement(block);
        const auto compact_wire = encode_p2p_frame({
            stage7_protocol_version, P2pMessageType::compact_block, 3U,
            encode_compact_block_announcement(announcement)});
        check(send_all(client, inventory_wire) && send_all(client, compact_wire),
              "inventory and compact announcement sent over live connection");
        const auto received_inventory_frame = receive_next(server, stream);
        const auto received_inventory = decode_inventory(
            received_inventory_frame.frame.payload, {8U, 16U});
        check(received_inventory_frame.status == FrameStreamStatus::frame_ready &&
              received_inventory_frame.frame.type ==
                  P2pMessageType::transaction_inventory &&
              received_inventory && *received_inventory == inventory,
              "live transaction inventory decoded");
        const auto received_compact_frame = receive_next(server, stream);
        const auto received_announcement = decode_compact_block_announcement(
            received_compact_frame.frame.payload, 8U, 1024U);
        check(received_compact_frame.status == FrameStreamStatus::frame_ready &&
              received_announcement.has_value(),
              "live compact block announcement decoded");

        ValidatedRelayPool server_pool(8U, 1024U);
        check(server_pool.remember_validated(block.transactions.front()),
              "server has one locally admitted transaction");
        BlockChunkLimits chunk_limits;
        chunk_limits.maximum_chunk_bytes = 1024U;
        chunk_limits.maximum_transactions_per_chunk = 8U;
        chunk_limits.maximum_transaction_body_bytes = 128U;
        chunk_limits.maximum_total_chunks = 8U;
        chunk_limits.maximum_total_transactions = 8U;
        chunk_limits.maximum_total_transfer_bytes = 1024U;
        CompactDownloadResult immediate;
        CompactBlockDownload download(*received_announcement, server_pool,
            chunk_limits, [](const TransactionEnvelope& transaction) {
                return transaction.version == 1U && !transaction.body.empty();
            });
        const auto missing = download.start(immediate);
        check(missing && missing->indexes == std::vector<std::uint32_t>{1U},
              "live compact receiver identifies only missing transaction");

        const auto request_wire = encode_p2p_frame({
            stage7_protocol_version, P2pMessageType::get_block_transactions,
            4U, encode_missing_transaction_request(*missing)});
        check(send_all(server, request_wire),
              "missing transaction request sent back over live connection");
        FrameStreamDecoder client_stream({}, 512U * 1024U);
        const auto received_request_frame = receive_next(client, client_stream);
        const auto received_request = decode_missing_transaction_request(
            received_request_frame.frame.payload, 8U);
        check(received_request_frame.status == FrameStreamStatus::frame_ready &&
              received_request && received_request->indexes == missing->indexes,
              "miner receives exact missing indexes");
        const auto chunks = make_block_transaction_chunks(
            block, received_request->indexes, chunk_limits.maximum_chunk_bytes);
        check(chunks && chunks->size() == 1U,
              "miner creates bounded missing transaction chunk");
        const auto chunk_wire = encode_p2p_frame({
            stage7_protocol_version, P2pMessageType::block_transactions, 4U,
            encode_block_transaction_chunk(chunks->front())});
        check(send_all(client, chunk_wire),
              "missing transaction chunk sent over live connection");
        const auto received_chunk_frame = receive_next(server, stream);
        const auto received_chunk = decode_block_transaction_chunk(
            received_chunk_frame.frame.payload, chunk_limits);
        check(received_chunk_frame.status == FrameStreamStatus::frame_ready &&
              received_chunk.has_value(), "live transaction chunk decoded");
        const auto completed = download.add_chunk(*received_chunk);
        check(completed.block && encode_block(*completed.block) == encode_block(block),
              "live socket flow reconstructs exact compact block");

        FrameStreamDecoder corrupted({}, 1024U);
        auto bad_wire = wire;
        bad_wire[16U] = 0xffU;
        bad_wire[17U] = 0xffU;
        bad_wire[18U] = 0xffU;
        bad_wire[19U] = 0x7fU;
        check(corrupted.feed(bad_wire.data(), p2p_frame_header_size),
              "bounded decoder accepts only small header allocation");
        const auto bad_result = corrupted.next();
        check(bad_result.status == FrameStreamStatus::failed &&
              bad_result.error == P2pFrameError::oversized_payload,
              "huge declared payload rejected before body allocation");

        PeerResourceGuard guard({100U, 2U, 1U, 10U});
        check(guard.reserve_bytes(80U) && !guard.reserve_bytes(21U),
              "peer send queue applies backpressure");
        guard.release_bytes(30U);
        check(guard.queued_bytes() == 50U && guard.reserve_bytes(50U),
              "released queue capacity can be reused");
        check(guard.begin_request(1U) && !guard.begin_request(1U) &&
              guard.begin_request(2U) && !guard.begin_request(3U),
              "request IDs are unique and bounded");
        guard.finish_request(1U);
        check(guard.begin_request(3U), "completed request releases capacity");
        check(guard.begin_validation() && !guard.begin_validation(),
              "expensive validation jobs are bounded");
        guard.finish_validation();
        guard.penalize(4U);
        guard.penalize(6U);
        check(guard.banned() && guard.score() == 10U,
              "misbehavior threshold bans peer");

        std::cout << checks << " P2P transport checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
