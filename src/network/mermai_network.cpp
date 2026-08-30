/*
 * MERMAI P2P Network Layer
 * 
 * Creator: dominikdev-glitch
 * Peer discovery, message propagation, quorum vote broadcasting, and network synchronization
 */

#include "../include/mermai/mermai_network.hpp"
#include "../include/mermai/mermai_db.hpp"
#include "../include/mermai/mermai_consensus.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mermai {

namespace {

constexpr uint32_t MAX_P2P_MESSAGE_SIZE = 4U * 1024U * 1024U;
constexpr uint32_t MAX_CHAIN_BLOCKS_PER_MESSAGE = 4096;
constexpr uint32_t MAX_SERIALIZED_BLOCK_SIZE = 1U * 1024U * 1024U;

bool send_all(std::uintptr_t socket_handle, const uint8_t* data, size_t size) {
    while (size > 0) {
#ifdef _WIN32
        const int sent = send(static_cast<SOCKET>(socket_handle), reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
#else
        const int sent = static_cast<int>(send(static_cast<int>(socket_handle), data, size, 0));
#endif
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool receive_all(std::uintptr_t socket_handle, uint8_t* data, size_t size) {
    while (size > 0) {
#ifdef _WIN32
        const int received = recv(static_cast<SOCKET>(socket_handle), reinterpret_cast<char*>(data), static_cast<int>(size), 0);
#else
        const int received = static_cast<int>(recv(static_cast<int>(socket_handle), data, size, 0));
#endif
        if (received <= 0) return false;
        data += received;
        size -= static_cast<size_t>(received);
    }
    return true;
}

void close_socket(std::uintptr_t socket_handle) {
    if (!socket_handle) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket_handle));
#else
    close(static_cast<int>(socket_handle));
#endif
}

void append_u32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value >> 24));
    output.push_back(static_cast<uint8_t>(value >> 16));
    output.push_back(static_cast<uint8_t>(value >> 8));
    output.push_back(static_cast<uint8_t>(value));
}

void append_string(std::vector<uint8_t>& output, const std::string& value) {
    append_u32(output, static_cast<uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

std::vector<uint8_t> encode_chain_payload(const std::vector<std::shared_ptr<mermai_block>>& blocks) {
    std::vector<uint8_t> payload;
    append_u32(payload, static_cast<uint32_t>(blocks.size()));
    for (const auto& block : blocks) {
        if (!block) continue;
        const auto serialized = block->serialize();
        append_u32(payload, static_cast<uint32_t>(serialized.size()));
        payload.insert(payload.end(), serialized.begin(), serialized.end());
    }
    return payload;
}

std::vector<std::shared_ptr<mermai_block>> decode_chain_payload(const std::vector<uint8_t>& payload) {
    if (payload.size() < 4) return {};

    size_t offset = 0;
    const uint32_t block_count = (static_cast<uint32_t>(payload[offset]) << 24) |
                                 (static_cast<uint32_t>(payload[offset + 1]) << 16) |
                                 (static_cast<uint32_t>(payload[offset + 2]) << 8) |
                                 static_cast<uint32_t>(payload[offset + 3]);
    offset += 4;
    if (block_count == 0 || block_count > MAX_CHAIN_BLOCKS_PER_MESSAGE) return {};

    std::vector<std::shared_ptr<mermai_block>> blocks;
    blocks.reserve(block_count);
    for (uint32_t index = 0; index < block_count; ++index) {
        if (offset + 4 > payload.size()) return {};
        const uint32_t block_size = (static_cast<uint32_t>(payload[offset]) << 24) |
                                    (static_cast<uint32_t>(payload[offset + 1]) << 16) |
                                    (static_cast<uint32_t>(payload[offset + 2]) << 8) |
                                    static_cast<uint32_t>(payload[offset + 3]);
        offset += 4;
        if (block_size == 0 || block_size > MAX_SERIALIZED_BLOCK_SIZE || offset + block_size > payload.size()) return {};

        std::string serialized(reinterpret_cast<const char*>(&payload[offset]), block_size);
        offset += block_size;
        try {
            blocks.push_back(std::make_shared<mermai_block>(mermai_block::deserialize(serialized)));
        } catch (const std::exception&) {
            return {};
        }
    }

    return offset == payload.size() ? blocks : std::vector<std::shared_ptr<mermai_block>>{};
}

P2PMessage hello_message(const std::string& node_id, uint16_t port, const std::shared_ptr<mermai_blockchain>& blockchain) {
    P2PMessage message;
    message.type = mermai_message_type::HELLO;
    append_u32(message.payload, 1);
    append_string(message.payload, node_id);
    message.payload.push_back(static_cast<uint8_t>(port >> 8));
    message.payload.push_back(static_cast<uint8_t>(port));
    append_u32(message.payload, blockchain ? blockchain->get_height() : 0);
    return message;
}

}

// ============================================================================
// PEER IMPLEMENTATION
// ============================================================================

mermai_peer::mermai_peer(const std::string& id, const std::string& addr, uint16_t p, std::uintptr_t socket)
    : peer_id(id), address(addr), port(p), last_seen(std::time(nullptr)), socket_handle(socket) {}

void mermai_peer::send_message(const P2PMessage& msg) {
    if (!socket_handle) return;
    const auto data = msg.serialize();
    send_all(socket_handle, data.data(), data.size());
}

P2PMessage mermai_peer::receive_message() {
    P2PMessage msg;
    if (!socket_handle) return msg;
    std::vector<uint8_t> header(5);
    if (!receive_all(socket_handle, header.data(), header.size())) return msg;
    const uint32_t payload_size = (static_cast<uint32_t>(header[1]) << 24) | (static_cast<uint32_t>(header[2]) << 16) | (static_cast<uint32_t>(header[3]) << 8) | header[4];
    if (payload_size > MAX_P2P_MESSAGE_SIZE) return msg;
    std::vector<uint8_t> data(5 + payload_size);
    std::copy(header.begin(), header.end(), data.begin());
    if (payload_size && !receive_all(socket_handle, data.data() + 5, payload_size)) return P2PMessage{};
    msg = P2PMessage::deserialize(data);
    update_last_seen();
    return msg;
}

void mermai_peer::disconnect() {
    close_socket(socket_handle);
    socket_handle = 0;
    std::cout << "Disconnecting peer " << peer_id << std::endl;
}

void mermai_peer::increase_reputation(int points) {
    reputation = std::min(100, (int)reputation + points);
}

void mermai_peer::decrease_reputation(int points) {
    reputation = std::max(0, (int)reputation - points);
}

// ============================================================================
// P2P MESSAGE IMPLEMENTATION
// ============================================================================

std::vector<uint8_t> P2PMessage::serialize() const {
    if (payload.size() > MAX_P2P_MESSAGE_SIZE) return {};
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(type));
    const uint32_t size = static_cast<uint32_t>(payload.size());
    
    data.push_back((size >> 24) & 0xFF);
    data.push_back((size >> 16) & 0xFF);
    data.push_back((size >> 8) & 0xFF);
    data.push_back(size & 0xFF);
    
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

P2PMessage P2PMessage::deserialize(const std::vector<uint8_t>& data) {
    P2PMessage msg;
    if (data.size() < 5) return msg;
    
    msg.type = static_cast<mermai_message_type>(data[0]);
    msg.payload_size = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
    
    if (data.size() >= 5 + msg.payload_size) {
        msg.payload.assign(data.begin() + 5, data.begin() + 5 + msg.payload_size);
    }
    
    return msg;
}

// ============================================================================
// P2P NODE IMPLEMENTATION
// ============================================================================

mermai_p2p_node::mermai_p2p_node(uint16_t port, std::shared_ptr<mermai_blockchain> chain)
    : listen_port(port), blockchain(chain) {
    node_id = "node_" + std::to_string(std::time(nullptr));
}

mermai_p2p_node::~mermai_p2p_node() {
    stop();
    for (auto& peer : peers) peer->disconnect();
}

bool mermai_p2p_node::start_listening() {
    if (listening) return false;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    const SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) { WSACleanup(); return false; }
#else
    const int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle < 0) return false;
#endif
    int reuse = 1;
    setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(listen_port);
    inet_pton(AF_INET, "0.0.0.0", &server_address.sin_addr);
    if (bind(socket_handle, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0 || listen(socket_handle, 64) < 0) {
        close_socket(static_cast<std::uintptr_t>(socket_handle));
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    listen_socket = static_cast<std::uintptr_t>(socket_handle);
    listening = true;
    std::cout << "P2P node listening on port " << listen_port << std::endl;
    listener_thread = std::thread([this]() {
        while (listening) {
            sockaddr_in client_address{};
#ifdef _WIN32
            int address_size = sizeof(client_address);
            const SOCKET client = accept(static_cast<SOCKET>(listen_socket), reinterpret_cast<sockaddr*>(&client_address), &address_size);
            if (client == INVALID_SOCKET) break;
#else
            socklen_t address_size = sizeof(client_address);
            const int client = accept(static_cast<int>(listen_socket), reinterpret_cast<sockaddr*>(&client_address), &address_size);
            if (client < 0) break;
#endif
            char ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &client_address.sin_addr, ip, INET_ADDRSTRLEN);
            if (is_banned(ip)) {
                close_socket(static_cast<std::uintptr_t>(client));
                continue;
            }
            handle_incoming_connection(static_cast<std::uintptr_t>(client));
        }
    });
    return true;
}

bool mermai_p2p_node::connect_to_peer(const std::string& address, uint16_t port) {
    if (is_banned(address)) return false;
#ifdef _WIN32
    const SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) return false;
#else
    const int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle < 0) return false;
#endif
    sockaddr_in peer_address{};
    peer_address.sin_family = AF_INET;
    peer_address.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &peer_address.sin_addr) != 1 || connect(socket_handle, reinterpret_cast<sockaddr*>(&peer_address), sizeof(peer_address)) < 0) {
        close_socket(static_cast<std::uintptr_t>(socket_handle));
        return false;
    }
    auto peer = std::make_shared<mermai_peer>(address + ":" + std::to_string(port), address, port, static_cast<std::uintptr_t>(socket_handle));
    add_peer(peer);
    peer->send_message(hello_message(node_id, listen_port, blockchain));
    std::thread([this, peer]() {
        while (listening) {
            const auto message = peer->receive_message();
            if (message.payload.empty() && message.type != mermai_message_type::HELLO) break;
            handle_message(peer, message);
        }
    }).detach();
    return true;
}

void mermai_p2p_node::broadcast_block(const std::shared_ptr<mermai_block>& block) {
    P2PMessage msg;
    msg.type = mermai_message_type::BLOCK;
    if (block) {
        const auto serialized = block->serialize();
        msg.payload.assign(serialized.begin(), serialized.end());
    }
    
    for (auto& peer : peers) {
        peer->send_message(msg);
    }
    
    std::cout << "Broadcast block to " << peers.size() << " peers" << std::endl;
}

void mermai_p2p_node::broadcast_transaction(const std::shared_ptr<mermai_transaction>& tx) {
    P2PMessage msg;
    msg.type = mermai_message_type::NEW_TRANSACTION;
    if (tx) {
        const auto serialized = tx->serialize();
        msg.payload.assign(serialized.begin(), serialized.end());
    }
    
    for (auto& peer : peers) {
        peer->send_message(msg);
    }
}

void mermai_p2p_node::broadcast_vote(const mermai_block_vote& vote) {
    P2PMessage msg;
    msg.type = mermai_message_type::BLOCK_VOTE;
    const auto serialized = vote.serialize();
    msg.payload.assign(serialized.begin(), serialized.end());
    for (auto& peer : peers) {
        peer->send_message(msg);
    }
}

bool mermai_p2p_node::sync_blocks() {
    std::cout << "Syncing blocks with peers..." << std::endl;
    P2PMessage message;
    message.type = mermai_message_type::GET_BLOCKS;
    for (auto& peer : peers) peer->send_message(message);
    return true;
}

void mermai_p2p_node::add_peer(std::shared_ptr<mermai_peer> peer) {
    if (peers.size() < MAX_PEERS) {
        peers.push_back(peer);
        std::cout << "Added peer: " << peer->get_id() << std::endl;
    }
}

void mermai_p2p_node::remove_peer(const std::string& peer_id) {
    peers.erase(
        std::remove_if(peers.begin(), peers.end(),
            [&peer_id](const std::shared_ptr<mermai_peer>& p) { return p->get_id() == peer_id; }),
        peers.end()
    );
}

std::shared_ptr<mermai_peer> mermai_p2p_node::get_peer(const std::string& peer_id) {
    for (auto& peer : peers) {
        if (peer->get_id() == peer_id) return peer;
    }
    return nullptr;
}

void mermai_p2p_node::ban_peer(const std::string& address) {
    banned_ips.insert(address);
    std::cout << "[BAN] Banned peer address: " << address << std::endl;
}

bool mermai_p2p_node::is_banned(const std::string& address) const {
    return banned_ips.find(address) != banned_ips.end();
}

void mermai_p2p_node::request_peer_list() {
    std::cout << "Requesting peer list..." << std::endl;
}

void mermai_p2p_node::handle_incoming_connection(std::uintptr_t socket_handle) {
    auto peer = std::make_shared<mermai_peer>("incoming-" + std::to_string(socket_handle), "incoming", 0, socket_handle);
    add_peer(peer);
    std::thread([this, peer]() {
        while (listening) {
            const auto message = peer->receive_message();
            if (message.type == mermai_message_type::HELLO) {
                peer->send_message(hello_message(node_id, listen_port, blockchain));
            } else if (!message.payload.empty()) {
                handle_message(peer, message);
            } else {
                break;
            }
        }
        peer->disconnect();
    }).detach();
}

void mermai_p2p_node::handle_message(std::shared_ptr<mermai_peer> peer, const P2PMessage& msg) {
    switch (msg.type) {
        case mermai_message_type::GET_BLOCKS: {
            std::vector<std::shared_ptr<mermai_block>> local_chain;
            if (blockchain) {
                for (uint32_t height = 0; height <= blockchain->get_height(); ++height) {
                    if (const auto block = blockchain->get_block(height)) {
                        local_chain.push_back(block);
                    }
                }
            }
            P2PMessage response;
            response.type = mermai_message_type::BLOCKS;
            response.payload = encode_chain_payload(local_chain);
            peer->send_message(response);
            break;
        }
        case mermai_message_type::BLOCKS: {
            if (!blockchain) break;
            const auto candidate = decode_chain_payload(msg.payload);
            if (candidate.empty()) {
                if (peer) peer->decrease_reputation(10);
                break;
            }
            if (database) {
                const uint32_t local_height = blockchain->get_height();
                if (candidate.size() <= local_height + 1) break;
                bool accepted = true;
                for (uint32_t height = 0; height <= local_height; ++height) {
                    const auto local = blockchain->get_block(height);
                    if (!local || !candidate[height] || local->serialize() != candidate[height]->serialize()) {
                        accepted = false;
                        break;
                    }
                }
                for (size_t index = static_cast<size_t>(local_height) + 1; accepted && index < candidate.size(); ++index) {
                    const auto& block = candidate[index];
                    accepted = blockchain->can_add_block(block) && database->apply_block(block) && blockchain->add_block(block);
                }
                if (!accepted && peer) peer->decrease_reputation(10);
                break;
            }
            if (!blockchain->replace_chain_if_longer(candidate)) {
                if (peer) peer->decrease_reputation(10);
            }
            break;
        }
        case mermai_message_type::BLOCK:
            try {
                const std::string serialized(msg.payload.begin(), msg.payload.end());
                auto block = std::make_shared<mermai_block>(mermai_block::deserialize(serialized));
                const bool accepted = blockchain && blockchain->can_add_block(block) &&
                    (!database || database->apply_block(block)) && blockchain->add_block(block);
                if (!accepted && peer) {
                    peer->decrease_reputation(10);
                }
            } catch (const std::exception&) {
                if (peer) peer->decrease_reputation(10);
            }
            break;
        case mermai_message_type::NEW_TRANSACTION:
            try {
                const std::string serialized(msg.payload.begin(), msg.payload.end());
                mermai_transaction tx = mermai_transaction::deserialize(serialized);
                if (database && tx.verify()) {
                    database->add_to_mempool(tx);
                }
            } catch (const std::exception&) {}
            break;
        case mermai_message_type::BLOCK_VOTE:
            try {
                const std::string serialized(msg.payload.begin(), msg.payload.end());
                mermai_block_vote vote = mermai_block_vote::deserialize(serialized);
                if (quorum && consensus) {
                    quorum->add_vote(vote, *consensus);
                }
            } catch (const std::exception&) {}
            break;
        default:
            break;
    }
}

void mermai_p2p_node::stop() {
    if (!listening) return;
    listening = false;
    close_socket(listen_socket);
    listen_socket = 0;
    if (listener_thread.joinable()) listener_thread.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================================
// PEER DISCOVERY IMPLEMENTATION
// ============================================================================

const std::vector<mermai_bootstrap_node>& mermai_peer_discovery::get_bootstrap_nodes() {
    static std::vector<mermai_bootstrap_node> bootstrap_nodes;
    
    if (bootstrap_nodes.empty()) {
        bootstrap_nodes.push_back({
            "127.0.0.1", 6333, "bootstrap-node-1"
        });
    }
    
    return bootstrap_nodes;
}

bool mermai_peer_discovery::discover_peers(std::shared_ptr<mermai_p2p_node> node) {
    const auto& bootstrap_nodes = get_bootstrap_nodes();
    for (const auto& bootstrap : bootstrap_nodes) {
        node->connect_to_peer(bootstrap.address, bootstrap.port);
    }
    return true;
}


// ============================================================================
// PHASE 4: PEER REPUTATION, RATE LIMITER, ECLIPSE RESISTANCE
// ============================================================================

void mermai_p2p_node::update_peer_reputation(const std::string& ip, int delta) {
    peer_reputation_map[ip] += delta;
    // Auto-ban at -100
    if (peer_reputation_map[ip] <= -100) {
        banned_ips.insert(ip);
        std::cout << "[BAN] Peer " << ip << " auto-banned (reputation "
                  << peer_reputation_map[ip] << ")" << std::endl;
    }
}

int mermai_p2p_node::get_peer_reputation(const std::string& ip) const {
    auto it = peer_reputation_map.find(ip);
    return (it != peer_reputation_map.end()) ? it->second : 0;
}

bool mermai_p2p_node::is_peer_banned(const std::string& ip) const {
    return banned_ips.count(ip) > 0;
}

bool mermai_p2p_node::allow_block_from_peer(const std::string& ip) {
    int& count = block_rate_counts[ip];
    if (count >= RATE_LIMIT_PER_WINDOW) {
        return false;
    }
    ++count;
    return true;
}

std::string mermai_p2p_node::get_subnet(const std::string& ip) {
    // Extract /24 subnet from dotted-decimal IPv4
    auto pos = ip.rfind('.');
    if (pos == std::string::npos) return ip;
    return ip.substr(0, pos);
}

bool mermai_p2p_node::allow_subnet_connection(const std::string& ip) {
    std::string subnet = get_subnet(ip);
    int& count = subnet_connection_counts[subnet];
    if (count >= MAX_PEERS_PER_SUBNET) {
        return false;
    }
    ++count;
    return true;
}

} // namespace mermai
