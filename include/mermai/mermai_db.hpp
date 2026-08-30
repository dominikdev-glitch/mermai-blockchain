#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include "mermai_blockchain.hpp"

struct sqlite3;

namespace mermai {

// ============================================================================
// STATE DATABASE
// ============================================================================

/**
 * mermai_db: Persistent state storage
 * - Uses SQLite for ACID guarantees
 * - Stores blocks, transactions, accounts, contracts
 * - Key-value for contract storage
 */

class mermai_db {
private:
    std::string db_path;
    sqlite3* db_connection = nullptr;
    
public:
    explicit mermai_db(const std::string& path);
    ~mermai_db();
    
    // Initialize database schema
    bool initialize();
    
    // ========== BLOCK OPERATIONS ==========
    bool save_block(const std::shared_ptr<mermai_block>& block);
    // Validate and commit a block plus its UTXO changes in one SQLite transaction.
    bool apply_block(const std::shared_ptr<mermai_block>& block);
    std::shared_ptr<mermai_block> get_block_by_hash(const std::string& hash);
    std::shared_ptr<mermai_block> get_block_by_height(uint32_t height);
    uint32_t get_latest_block_height();
    
    // ========== TRANSACTION OPERATIONS ==========
    bool save_transaction(const mermai_transaction& tx);
    mermai_transaction get_transaction(const std::string& tx_id);
    std::vector<mermai_transaction> get_transactions_in_block(const std::string& block_hash);

    // ========== UTXO STATE ===========
    // The chain uses transaction inputs/outputs, so spendability is tracked as
    // an explicit persistent UTXO set rather than inferred from history.
    struct UnspentOutput {
        std::string tx_id;
        uint32_t output_index = 0;
        std::string address;
        uint64_t amount = 0;
    };

    bool save_unspent_output(const UnspentOutput& output);
    bool get_unspent_output(const std::string& tx_id, uint32_t output_index, UnspentOutput& output);
    bool spend_unspent_output(const std::string& tx_id, uint32_t output_index);
    bool validate_transaction_inputs(const mermai_transaction& tx) const;
    
    // ========== ACCOUNT OPERATIONS ==========
    struct Account {
        std::string address;
        uint64_t balance;
        uint32_t nonce;
        uint64_t last_activity;
    };
    
    bool save_account(const Account& account);
    Account get_account(const std::string& address);
    bool update_balance(const std::string& address, uint64_t new_balance);
    bool increment_nonce(const std::string& address);
    
    // ========== VALIDATOR OPERATIONS ==========
    bool save_validator(const mermai_validator_stake& stake);
    mermai_validator_stake get_validator(const std::string& address);
    std::vector<mermai_validator_stake> get_all_validators();
    bool update_validator_stake(const std::string& address, uint64_t new_stake);
    bool remove_validator(const std::string& address);
    
    // ========== CONTRACT STORAGE ==========
    bool save_contract_storage(
        const std::string& contract_address,
        const std::string& key,
        const std::vector<uint8_t>& value
    );
    
    std::vector<uint8_t> get_contract_storage(
        const std::string& contract_address,
        const std::string& key
    );
    
    std::map<std::string, std::vector<uint8_t>> get_contract_all_storage(
        const std::string& contract_address
    );
    bool save_contract_code(const std::string& contract_address, const std::vector<uint8_t>& code);
    std::vector<uint8_t> get_contract_code(const std::string& contract_address);
    
    // ========== MEMPOOL OPERATIONS ==========
    bool add_to_mempool(const mermai_transaction& tx);
    bool remove_from_mempool(const std::string& tx_id);
    std::vector<mermai_transaction> get_mempool(uint32_t max_count = 1000);
    void clear_mempool();
    
    // ========== TRANSACTION HISTORY ==========
    std::vector<mermai_transaction> get_account_transactions(
        const std::string& address,
        uint32_t limit = 100
    );
    
    // ========== STATE SNAPSHOT ==========
    bool save_state_root(uint32_t block_height, const std::string& state_root);
    std::string get_state_root(uint32_t block_height);
    
    // ========== UTILITY ==========
    bool sync();
    void backup(const std::string& backup_path);
    uint64_t get_db_size();
};

// ============================================================================
// KEY-VALUE STORE (for contract storage)
// ============================================================================

class mermai_key_value_store {
private:
    std::map<std::string, std::vector<uint8_t>> store;
    
public:
    bool set(const std::string& key, const std::vector<uint8_t>& value);
    std::vector<uint8_t> get(const std::string& key);
    bool exists(const std::string& key) const;
    bool delete_key(const std::string& key);
    void clear();
};

// ============================================================================
// MERKLE TREE for state verification
// ============================================================================

class mermai_merkle_tree {
public:
    static std::string calculate_tree_hash(const std::vector<mermai_transaction>& transactions);
    static std::string verify_tree(
        const std::vector<mermai_transaction>& transactions,
        const std::string& expected_root
    );
    
private:
    static std::string hash_combine(const std::string& left, const std::string& right);
};

} // namespace mermai
