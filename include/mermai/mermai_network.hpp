#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <atomic>
#include <thread>
#include <map>
#include <set>
#include "mermai_blockchain.hpp"

namespace mermai {

class mermai_db;
class mermai_time_weighted_pos;
class mermai_quorum_collector;

// ============================================================================
// P2P NETWORK PROTOCOL
// ============================================================================

enum class mermai_message_type : uint8_t {
    // Handshake
    HELLO = 0x00,
    PEER_LIST = 0x01,
    
    // Blockchain sync
    GET_BLOCKS = 0x10,
    BLOCKS = 0x11,
    GET_BLOCK = 0x12,
    BLOCK = 0x13,
    
    // Transactions
    MEMPOOL_SYNC = 0x20,
    NEW_TRANSACTION = 0x21,
    TRANSACTION = 0x23,
    
    // Consensus & Quorum
    BLOCK_PROPOSAL = 0x30,
    BLOCK_VOTE = 0x31,
    
    // Ping/Pong
    PING = 0xF0,
    PONG = 0xF1,
};

// ============================================================================
// MESSAGE STRUCTURES
// ============================================================================

struct P2PMessage {
    mermai_message_type type;
    uint32_t payload_size;
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> serialize() const;
    static P2PMessage deserialize(const std::vector<uint8_t>& data);
};

// ============================================================================
// P2P PEER
// ============================================================================

class mermai_peer {
private:
    std::string peer_id;
    std::string address;
    uint16_t port;
    uint64_t last_seen;
    uint32_t reputation = 100; // 0-100 score
    std::uintptr_t socket_handle = 0;
    
public:
    mermai_peer(const std::string& id, const std::string& addr, uint16_t p, std::uintptr_t socket = 0);
    
    void send_message(const P2PMessage& msg);
    P2PMessage receive_message();
    void disconnect();
    
    // Reputation management
    void increase_reputation(int points);
    void decrease_reputation(int points);
    bool is_trusted() const { return reputation > 50; }
    bool is_banned() const { return reputation == 0; }
    
    // Getters
    const std::string& get_id() const { return peer_id; }
    const std::string& get_address() const { return address; }
    uint64_t get_last_seen() const { return last_seen; }
    void update_last_seen() { last_seen = std::time(nullptr); }
};

// ============================================================================
// P2P NODE
// ============================================================================

class mermai_p2p_node {
private:
    std::string node_id;
    uint16_t listen_port;
    std::vector<std::shared_ptr<mermai_peer>> peers;
    std::set<std::string> banned_ips;
    std::shared_ptr<mermai_blockchain> blockchain;
    std::shared_ptr<mermai_db> database;
    std::shared_ptr<mermai_time_weighted_pos> consensus;
    std::shared_ptr<mermai_quorum_collector> quorum;
    
    static constexpr uint32_t MAX_PEERS = 100;
    static constexpr uint32_t MIN_PEERS = 8;
    
public:
    mermai_p2p_node(uint16_t port, std::shared_ptr<mermai_blockchain> chain);
    ~mermai_p2p_node();
    
    // Network operations
    bool start_listening();
    bool connect_to_peer(const std::string& address, uint16_t port);
    void broadcast_block(const std::shared_ptr<mermai_block>& block);
    void broadcast_transaction(const std::shared_ptr<mermai_transaction>& tx);
    void broadcast_vote(const mermai_block_vote& vote);
    bool sync_blocks();
    void stop();
    
    // Attach dependencies
    void set_database(std::shared_ptr<mermai_db> db) { database = std::move(db); }
    void set_consensus_and_quorum(std::shared_ptr<mermai_time_weighted_pos> pos, std::shared_ptr<mermai_quorum_collector> q) {
        consensus = std::move(pos);
        quorum = std::move(q);
    }
    
    // Peer management
    void add_peer(std::shared_ptr<mermai_peer> peer);
    void remove_peer(const std::string& peer_id);
    std::shared_ptr<mermai_peer> get_peer(const std::string& peer_id);
    uint32_t get_peer_count() const { return peers.size(); }
    void ban_peer(const std::string& address);
    bool is_banned(const std::string& address) const;
    
    // Discovery
    void request_peer_list();
    
    // Phase 4: Peer reputation scoring
    void update_peer_reputation(const std::string& ip, int delta);
    int  get_peer_reputation(const std::string& ip) const;
    bool is_peer_banned(const std::string& ip) const;

    // Phase 4: Rate limiter
    bool allow_block_from_peer(const std::string& ip);

    // Phase 4: Eclipse resistance (subnet diversity)
    bool allow_subnet_connection(const std::string& ip);

    static std::string get_subnet(const std::string& ip);

private:
    void handle_incoming_connection(std::uintptr_t socket_handle);
    void handle_message(std::shared_ptr<mermai_peer> peer, const P2PMessage& msg);
    std::atomic<bool> listening{false};
    std::uintptr_t listen_socket = 0;
    std::thread listener_thread;
    // Phase 4: Peer reputation map (ip -> score)
    std::map<std::string, int> peer_reputation_map;
    // Phase 4: Block rate limiter (ip -> count of blocks seen in current window)
    std::map<std::string, int> block_rate_counts;
    static constexpr int RATE_LIMIT_PER_WINDOW = 15;
    // Phase 4: Subnet connection counts (subnet -> count)
    std::map<std::string, int> subnet_connection_counts;
    static constexpr int MAX_PEERS_PER_SUBNET = 3;
};

// ============================================================================
// BOOTSTRAP NODES
// ============================================================================

struct mermai_bootstrap_node {
    std::string address;
    uint16_t port;
    std::string node_id;
};

class mermai_peer_discovery {
public:
    static const std::vector<mermai_bootstrap_node>& get_bootstrap_nodes();
    static bool discover_peers(std::shared_ptr<mermai_p2p_node> node);
};

} // namespace mermai
