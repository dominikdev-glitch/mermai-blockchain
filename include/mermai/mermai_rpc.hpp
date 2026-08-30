#pragma once

#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <cstdint>
#include "mermai_blockchain.hpp"
#include "mermai_network.hpp"
#include "mermai_db.hpp"
#include "mermai_consensus.hpp"

namespace mermai {

class mermai_rpc_server {
    // Phase 2 & 5 additions
    std::string suggest_fee();
    std::string get_metrics();

private:
    uint16_t port;
    std::shared_ptr<mermai_blockchain> blockchain;
    std::shared_ptr<mermai_p2p_node> p2p_node;
    std::shared_ptr<mermai_db> db;
    std::shared_ptr<mermai_time_weighted_pos> consensus;
    std::shared_ptr<mermai_quorum_collector> quorum;
    std::atomic<bool> server_running{false};
    std::uintptr_t server_socket = 0;
    std::thread server_thread;
    
public:
    mermai_rpc_server(
        uint16_t rpc_port,
        std::shared_ptr<mermai_blockchain> chain,
        std::shared_ptr<mermai_p2p_node> network,
        std::shared_ptr<mermai_db> database,
        std::shared_ptr<mermai_time_weighted_pos> consensus_engine = nullptr,
        std::shared_ptr<mermai_quorum_collector> quorum_collector = nullptr
    );
    ~mermai_rpc_server();
    
    bool start_server();
    void stop_server();
    std::string handle_request(const std::string& request);
    
    // ========== BLOCK METHODS ==========
    std::string get_block_count();
    std::string get_block(uint32_t height);
    std::string get_latest_block();
    std::string get_block_by_hash(const std::string& hash);
    std::string get_finalized_block();
    
    // ========== ACCOUNT METHODS ==========
    std::string get_account(const std::string& address);
    std::string get_balance(const std::string& address);
    std::string get_account_nonce(const std::string& address);
    std::string get_account_transactions(const std::string& address);
    
    // ========== TRANSACTION METHODS ==========
    std::string get_transaction(const std::string& tx_id);
    std::string send_transaction(const std::string& tx_json);
    std::string get_transaction_status(const std::string& tx_id);
    std::string estimate_gas(const std::string& call_json);
    
    // ========== CONTRACT METHODS ==========
    std::string deploy_contract(const std::string& deployment_json);
    std::string call_contract(const std::string& call_json);
    std::string get_contract_storage(const std::string& contract_address, const std::string& key);
    std::string get_contract_code(const std::string& contract_address);
    
    // ========== NETWORK METHODS ==========
    std::string get_peer_count();
    std::string get_network_info();
    std::string get_syncing_status();
    
    // ========== VALIDATOR & CONSENSUS METHODS ==========
    std::string register_validator(const std::string& validator_json);
    std::string get_validator_info(const std::string& address);
    std::string get_all_validators();
    std::string get_validator_weight(const std::string& address);
    std::string submit_vote(const std::string& vote_json);
    std::string get_quorum_status(const std::string& block_hash);
    
    // ========== UTILITY METHODS ==========
    std::string get_chain_id();
    std::string get_protocol_version();
    std::string get_mining_difficulty();
    
private:
    std::string handle_rpc_request(const std::string& request);
    std::string json_error(int code, const std::string& message);
};

} // namespace mermai
