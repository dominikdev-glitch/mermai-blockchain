/*
 * MERMAI Blockchain Network
 * 
 * Creator: dominikdev-glitch
 * Main Node Entry Point
 * - Initializes the blockchain, consensus engine, database, quorum collector, and P2P network
 * - Starts the JSON-RPC server
 * - Orchestrates full node operation with genesis loading
 */

#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_consensus.hpp"
#include "mermai/mermai_network.hpp"
#include "mermai/mermai_db.hpp"
#include "mermai/mermai_rpc.hpp"
#include <iostream>
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace mermai;

namespace {
std::string hash_to_hex(const mermai_hash256& hash) {
    std::ostringstream output;
    for (const auto byte : hash) output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
    return output.str();
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string s = hex;
    if (s.rfind("0x", 0) == 0) s = s.substr(2);
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        try {
            bytes.push_back(static_cast<uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
        } catch (...) {}
    }
    return bytes;
}
}

// ============================================================================
// MERMAI NETWORK NODE
// ============================================================================

class MermaiNode {
private:
    std::shared_ptr<mermai_blockchain> blockchain;
    std::shared_ptr<mermai_time_weighted_pos> consensus;
    std::shared_ptr<mermai_quorum_collector> quorum;
    std::shared_ptr<mermai_p2p_node> p2p_network;
    std::shared_ptr<mermai_db> database;
    std::shared_ptr<mermai_rpc_server> rpc_server;
    
    uint16_t p2p_port = 6333;
    uint16_t rpc_port = 6334;
    std::string db_name = "mermai.db";
    std::string genesis_path;
    bool running = false;
    
public:
    MermaiNode(uint16_t p2p = 6333, uint16_t rpc = 6334, const std::string& db = "mermai.db", const std::string& genesis = "")
        : p2p_port(p2p), rpc_port(rpc), db_name(db), genesis_path(genesis) {
        std::cout << "╔════════════════════════════════════╗" << std::endl;
        std::cout << "║      MERMAI BLOCKCHAIN NETWORK     ║" << std::endl;
        std::cout << "║   Time-Weighted Proof-of-Stake     ║" << std::endl;
        std::cout << "║         v1.0.0 Mainnet             ║" << std::endl;
        std::cout << "╚════════════════════════════════════╝" << std::endl << std::endl;
    }
    
    bool load_genesis(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[WARN] Genesis file not found: " << path << std::endl;
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::cout << "[INIT] Loaded genesis configuration from " << path << std::endl;
        return true;
    }

    bool initialize() {
        std::cout << "[INIT] Initializing Mermai Node..." << std::endl;
        
        // Initialize blockchain
        blockchain = std::make_shared<mermai_blockchain>();
        std::cout << "[INIT] ✓ Blockchain initialized (Genesis block created)" << std::endl;
        
        // Initialize consensus & quorum
        consensus = std::make_shared<mermai_time_weighted_pos>();
        quorum = std::make_shared<mermai_quorum_collector>();
        std::cout << "[INIT] ✓ Time-Weighted PoS & Quorum Collector initialized" << std::endl;
        
        // Initialize database
        database = std::make_shared<mermai_db>(db_name);
        if (!database->initialize()) {
            std::cerr << "[ERROR] Failed to initialize database" << std::endl;
            return false;
        }
        for (const auto& validator : database->get_all_validators()) {
            if (!consensus->restore_validator(validator)) {
                std::cerr << "[ERROR] Invalid persisted validator " << validator.address << std::endl;
                return false;
            }
        }
        blockchain->set_consensus_engine(consensus);
        
        if (!genesis_path.empty()) {
            load_genesis(genesis_path);
        }

        const uint32_t stored_height = database->get_latest_block_height();
        if (stored_height > 0) {
            std::vector<std::shared_ptr<mermai_block>> persisted_chain{blockchain->get_block(0)};
            for (uint32_t height = 1; height <= stored_height; ++height) {
                const auto block = database->get_block_by_height(height);
                if (!block) {
                    std::cerr << "[ERROR] Missing persisted block at height " << height << std::endl;
                    return false;
                }
                persisted_chain.push_back(block);
            }
            if (!blockchain->replace_chain_if_longer(persisted_chain)) {
                std::cerr << "[ERROR] Persisted chain failed validation" << std::endl;
                return false;
            }
        }
        std::cout << "[INIT] ✓ Database initialized (" << db_name << ")" << std::endl;
        
        // Initialize P2P network
        p2p_network = std::make_shared<mermai_p2p_node>(p2p_port, blockchain);
        p2p_network->set_database(database);
        p2p_network->set_consensus_and_quorum(consensus, quorum);
        if (!p2p_network->start_listening()) {
            std::cerr << "[ERROR] Failed to start P2P network on port " << p2p_port << std::endl;
            return false;
        }
        std::cout << "[INIT] ✓ P2P Network started (port " << p2p_port << ")" << std::endl;
        
        // Initialize RPC server
        rpc_server = std::make_shared<mermai_rpc_server>(rpc_port, blockchain, p2p_network, database, consensus, quorum);
        if (!rpc_server->start_server()) {
            std::cerr << "[ERROR] Failed to start RPC server on port " << rpc_port << std::endl;
            return false;
        }
        std::cout << "[INIT] ✓ JSON-RPC Server started (port " << rpc_port << ")" << std::endl;
        
        return true;
    }
    
    bool start() {
        std::cout << std::endl << "[START] Starting Mermai Node..." << std::endl;
        running = true;
        
        std::thread consensus_thread([this]() {
            this->consensus_loop();
        });
        
        std::thread sync_thread([this]() {
            this->network_sync_loop();
        });
        
        consensus_thread.detach();
        sync_thread.detach();
        
        std::cout << "[START] ✓ Mermai Node started" << std::endl;
        std::cout << std::endl << "Node Status:" << std::endl;
        std::cout << "  - P2P Listen: tcp://127.0.0.1:" << p2p_port << std::endl;
        std::cout << "  - RPC Endpoint: http://127.0.0.1:" << rpc_port << std::endl;
        std::cout << "  - Network ID: 1337" << std::endl;
        std::cout << "  - Consensus: Time-Weighted Proof-of-Stake with Quorum Finality" << std::endl;
        std::cout << std::endl;
        
        return true;
    }
    
    void consensus_loop() {
        std::cout << "[CONSENSUS] Starting block production loop..." << std::endl;
        
        while (running) {
            try {
                constexpr uint64_t block_interval_seconds = 10;
                uint64_t current_time = std::time(nullptr);
                const uint64_t slot_time = current_time - (current_time % block_interval_seconds);
                
                std::string proposer = consensus->select_block_proposer(slot_time);
                
                if (!proposer.empty()) {
                    std::cout << "[BLOCK] Proposer selected: " << proposer.substr(0, std::min<size_t>(8, proposer.size())) 
                              << "..." << std::endl;
                    
                    auto new_block = std::make_shared<mermai_block>(blockchain->get_height() + 1);
                    new_block->timestamp = slot_time;
                    auto latest = blockchain->get_latest_block();
                    
                    if (latest) {
                        new_block->prev_block_hash = hash_to_hex(latest->calculate_hash());
                        new_block->validator_address = proposer;
                        for (const auto& validator : consensus->get_consensus_state().active_validators) {
                            if (validator.address == proposer) {
                                new_block->validator_stake = validator.amount;
                                new_block->validator_age = slot_time > validator.lock_time ? slot_time - validator.lock_time : 0;
                                new_block->validator_public_key = validator.public_key;
                                break;
                            }
                        }
                        
                        if (blockchain->can_add_block(new_block) && database->apply_block(new_block) && blockchain->add_block(new_block)) {
                            std::cout << "[BLOCK] ✓ Block #" << new_block->height 
                                      << " produced successfully" << std::endl;
                            
                            consensus->finalize_block(new_block, proposer);
                            p2p_network->broadcast_block(new_block);
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(10));
                
            } catch (const std::exception& e) {
                std::cerr << "[CONSENSUS] Error: " << e.what() << std::endl;
            }
        }
    }
    
    void network_sync_loop() {
        std::cout << "[SYNC] Starting network sync loop..." << std::endl;
        
        while (running) {
            try {
                if (p2p_network->get_peer_count() < 8) {
                    mermai_peer_discovery::discover_peers(p2p_network);
                }
                p2p_network->sync_blocks();
                std::this_thread::sleep_for(std::chrono::seconds(30));
            } catch (const std::exception& e) {
                std::cerr << "[SYNC] Error: " << e.what() << std::endl;
            }
        }
    }
    
    void stop() {
        std::cout << "[STOP] Shutting down Mermai Node..." << std::endl;
        running = false;
        if (rpc_server) rpc_server->stop_server();
        if (database) database->sync();
        std::cout << "[STOP] ✓ MERMAI Node stopped" << std::endl;
    }
    
    bool is_running() const { return running; }
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int main(int argc, char* argv[]) {
    uint16_t p2p_port = 6333;
    uint16_t rpc_port = 6334;
    std::string db_name = "mermai.db";
    std::string genesis_path = "";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--p2p-port" && i + 1 < argc) {
            p2p_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--rpc-port" && i + 1 < argc) {
            rpc_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--db" && i + 1 < argc) {
            db_name = argv[++i];
        } else if (arg == "--genesis" && i + 1 < argc) {
            genesis_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Mermai Blockchain Node\n\n";
            std::cout << "Usage: mermai-node [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --p2p-port <port>   P2P network port (default: 6333)\n";
            std::cout << "  --rpc-port <port>   JSON-RPC server port (default: 6334)\n";
            std::cout << "  --db <filename>     Database filename (default: mermai.db)\n";
            std::cout << "  --genesis <file>    Genesis JSON configuration path\n";
            std::cout << "  --help              Show this help message\n";
            return 0;
        }
    }
    
    MermaiNode node(p2p_port, rpc_port, db_name, genesis_path);
    
    if (!node.initialize()) {
        std::cerr << "Failed to initialize node" << std::endl;
        return 1;
    }
    
    node.start();
    
    while (node.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
