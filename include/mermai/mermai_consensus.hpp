#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include "mermai_blockchain.hpp"

namespace mermai {

// ============================================================================
// TIME-WEIGHTED PROOF-OF-STAKE ENGINE
// ============================================================================

class mermai_time_weighted_pos {
private:
    mermai_consensus_state consensus_state;
    std::map<std::string, uint64_t> pending_rewards;
    uint64_t min_stake = 1000000;  // Minimum 1 unit to stake
    uint32_t min_lockup_blocks = 1000;
    
public:
    mermai_time_weighted_pos();
    
    // Validator Operations
    bool register_validator(
        const std::string& address,
        uint64_t stake_amount,
        const std::string& validator_name = "",
        const mermai_public_key& public_key = {}
    );
    
    bool unregister_validator(const std::string& address);
    bool restore_validator(const mermai_validator_stake& stake);
    
    float calculate_weight(const mermai_validator_stake& stake, uint64_t current_time) const;
    std::string select_block_proposer(uint64_t current_time) const;
    
    void finalize_block(
        const std::shared_ptr<mermai_block>& block,
        const std::string& proposer_address
    );
    
    void slash_validator(
        const std::string& address,
        float penalty_percentage,
        const std::string& reason
    );
    
    bool claim_rewards(const std::string& validator_address);
    uint64_t get_pending_rewards(const std::string& validator_address) const;

    // Convenience overload: slash_validator(address, percentage_0_to_100) without reason
    // penalty_percentage is in [0, 100] — e.g., 50 means 50%
    void slash_validator(const std::string& address, float penalty_percentage) {
        slash_validator(address, penalty_percentage / 100.0f, "equivocation");
    }

    // Apply inactivity penalty: deduct percentage of validator's current stake
    void apply_inactivity_penalty(const std::string& address, float penalty_percentage);

    const mermai_consensus_state& get_consensus_state() const { return consensus_state; }
    float get_validator_weight(const std::string& address, uint64_t current_time) const;
    uint32_t get_active_validator_count() const;
    uint64_t get_total_active_stake() const;
    
private:
    float calculate_time_multiplier(uint64_t stake_age_seconds) const;
    float calculate_performance_multiplier(const mermai_validator_stake& stake) const;
};

// ============================================================================
// QUORUM FINALITY COLLECTOR
// ============================================================================

class mermai_quorum_collector {
private:
    mutable std::mutex quorum_mutex;
    // block_hash -> set of valid votes
    std::map<std::string, std::vector<mermai_block_vote>> block_votes;
    // block_hash -> total voting stake
    std::map<std::string, uint64_t> vote_stake_totals;
    // finalized block hashes
    std::set<std::string> finalized_blocks;

public:
    mermai_quorum_collector() = default;

    // Adds and verifies a validator's vote for a block.
    // If voting stake >= 2/3 of active stake, block achieves quorum finality.
    bool add_vote(const mermai_block_vote& vote, const mermai_time_weighted_pos& consensus);

    bool is_block_finalized(const std::string& block_hash) const;
    uint64_t get_vote_stake(const std::string& block_hash) const;
    std::vector<mermai_block_vote> get_votes(const std::string& block_hash) const;
    void clear();
};

// ============================================================================
// BLOCK PROPOSAL VALIDATOR
// ============================================================================

class mermai_proposal_validator {
private:
    // Tracks (height, slot) -> block_hash to detect equivocation (double-proposals)
    static std::map<std::pair<uint32_t, uint64_t>, std::string> seen_proposals;
    static std::mutex proposal_mutex;

public:
    static bool validate_proposal(
        const std::shared_ptr<mermai_block>& block,
        mermai_time_weighted_pos& consensus,
        uint64_t current_time
    );
    
    static bool validate_signature(const mermai_transaction& tx);
    static void reset_proposals();
    
private:
    static bool verify_proposer_is_authorized(
        const std::string& proposer,
        float validator_weight,
        uint64_t current_time
    );
};

// ============================================================================
// REWARD DISTRIBUTION
// ============================================================================

struct mermai_block_reward {
    std::string recipient;
    uint64_t amount;
    std::string reason;
};

class mermai_reward_calculator {
public:
    static std::vector<mermai_block_reward> calculate_block_rewards(
        const std::shared_ptr<mermai_block>& block,
        uint32_t block_height,
        const mermai_time_weighted_pos& consensus
    );
    
    static uint64_t get_base_reward(uint32_t block_height);
    static std::vector<mermai_block_reward> distribute_fees(
        const std::vector<mermai_transaction>& transactions,
        const std::string& proposer
    );
};

} // namespace mermai
