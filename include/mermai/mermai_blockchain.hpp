#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <openssl/evp.h>

namespace mermai {

class mermai_time_weighted_pos;

// ============================================================================
// PRIMITIVE TYPES
// ============================================================================

using mermai_hash256 = std::vector<uint8_t>;
using mermai_signature = std::vector<uint8_t>;
using mermai_public_key = std::vector<uint8_t>;

// ============================================================================
// TRANSACTION
// ============================================================================

struct mermai_tx_input {
    std::string prev_tx_hash;
    uint32_t prev_output_idx;
    mermai_signature signature;
    mermai_public_key public_key;
};

struct mermai_tx_output {
    std::string address;
    uint64_t amount;
};

struct mermai_transaction {
    std::string id;
    std::vector<mermai_tx_input> inputs;
    std::vector<mermai_tx_output> outputs;
    uint64_t fee = 0;
    uint64_t timestamp = 0;
    
    std::string serialize() const;
    static mermai_transaction deserialize(const std::string& data);
    std::string get_hash() const;
    bool verify() const;
    bool sign(size_t input_idx, EVP_PKEY* private_key);
};

// ============================================================================
// BLOCK VOTE (Quorum Finality Attestation)
// ============================================================================

struct mermai_block_vote {
    std::string validator_address;
    std::string block_hash;
    uint32_t block_height = 0;
    uint64_t timestamp = 0;
    mermai_public_key validator_public_key;
    mermai_signature signature;
    
    std::string signing_payload() const;
    bool sign(EVP_PKEY* private_key);
    bool verify_signature() const;
    std::string serialize() const;
    static mermai_block_vote deserialize(const std::string& data);
};

// ============================================================================
// BLOCK
// ============================================================================

struct mermai_block {
    uint32_t version = 1;
    uint32_t height = 0;
    std::string prev_block_hash;
    std::string merkle_root;
    uint64_t timestamp = 0;
    uint32_t nonce = 0;
    
    // Proof-of-Stake fields
    std::string validator_address;
    uint64_t validator_stake = 0;
    uint64_t validator_age = 0;
    mermai_public_key validator_public_key;
    mermai_signature validator_signature;
    
    std::vector<mermai_transaction> transactions;
    
    mermai_block();
    explicit mermai_block(uint32_t block_height);
    
    std::string serialize() const;
    std::string signing_payload() const;
    bool sign(EVP_PKEY* private_key);
    bool verify_validator_signature() const;
    static mermai_block deserialize(const std::string& data);
    mermai_hash256 calculate_hash() const;
    bool verify_time_weight_pos() const;
    
    bool is_valid_proposer(
        const std::string& address,
        uint64_t stake_amount,
        uint64_t stake_age_seconds
    ) const;
};

// ============================================================================
// BLOCKCHAIN
// ============================================================================

class mermai_blockchain {
private:
    std::vector<std::shared_ptr<mermai_block>> chain;
    uint32_t difficulty = 4;
    std::shared_ptr<const mermai_time_weighted_pos> consensus_engine;

public:
    mermai_blockchain();
    void set_consensus_engine(std::shared_ptr<const mermai_time_weighted_pos> consensus) { consensus_engine = std::move(consensus); }
    
    bool can_add_block(const std::shared_ptr<mermai_block>& block) const;
    bool add_block(std::shared_ptr<mermai_block> block);
    bool replace_chain_if_longer(const std::vector<std::shared_ptr<mermai_block>>& candidate_chain);
    std::shared_ptr<mermai_block> get_block(uint32_t height) const;
    std::shared_ptr<mermai_block> get_latest_block() const;
    uint32_t get_height() const;
    bool is_valid() const;
    bool is_chain_valid() const;
    
    uint32_t calculate_difficulty();
};

// ============================================================================
// CONSENSUS STATE (Time-Weighted PoS)
// ============================================================================

struct mermai_validator_stake {
    std::string address;
    uint64_t amount = 0;
    uint64_t lock_time = 0;        // When stake was locked
    uint64_t last_proposal = 0;    // Last time validator proposed block
    float weight = 0.0f;          // Calculated from stake + age
    mermai_public_key public_key;
};

class mermai_consensus_state {
public:
    std::vector<mermai_validator_stake> active_validators;
    
    float calculate_validator_weight(const mermai_validator_stake& stake) const;
    std::string select_proposer() const;
    void slash_validator(const std::string& address, float percentage);
    void add_validator(const std::string& address, uint64_t amount);
    void remove_validator(const std::string& address);
};

} // namespace mermai
