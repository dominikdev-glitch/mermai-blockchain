/*
 * MERMAI Consensus Engine
 * 
 * Creator: dominikdev-glitch
 * Time-Weighted Proof-of-Stake (TW-PoS) validator selection, key enforcement, quorum finality, and rewards
 */

#include "../include/mermai/mermai_consensus.hpp"
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace mermai {
namespace {
std::string hash_to_hex(const mermai_hash256& hash) {
    std::ostringstream ss;
    for (auto b : hash) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}
}


// ============================================================================
// TIME-WEIGHTED PROOF-OF-STAKE IMPLEMENTATION
// ============================================================================

mermai_time_weighted_pos::mermai_time_weighted_pos() {}

bool mermai_time_weighted_pos::register_validator(
    const std::string& address,
    uint64_t stake_amount,
    const std::string& validator_name,
    const mermai_public_key& public_key
) {
    if (stake_amount < min_stake || address.empty()) {
        return false;
    }
    
    // In Phase 3: require valid ECDSA public key for security & block signatures
    if (!public_key.empty()) {
        const unsigned char* key_data = public_key.data();
        EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &key_data, static_cast<long>(public_key.size()));
        if (!pkey) return false;
        EVP_PKEY_free(pkey);
    }
    
    // Check if already registered
    for (const auto& v : consensus_state.active_validators) {
        if (v.address == address) return false;
    }
    
    consensus_state.add_validator(address, stake_amount);
    consensus_state.active_validators.back().public_key = public_key;
    return true;
}

bool mermai_time_weighted_pos::unregister_validator(const std::string& address) {
    auto it = std::find_if(
        consensus_state.active_validators.begin(),
        consensus_state.active_validators.end(),
        [&address](const mermai_validator_stake& v) { return v.address == address; }
    );
    
    if (it == consensus_state.active_validators.end()) return false;
    
    consensus_state.remove_validator(address);
    return true;
}

bool mermai_time_weighted_pos::restore_validator(const mermai_validator_stake& stake) {
    if (stake.address.empty() || stake.amount < min_stake || stake.lock_time == 0) return false;
    for (const auto& existing : consensus_state.active_validators) {
        if (existing.address == stake.address) return false;
    }
    consensus_state.active_validators.push_back(stake);
    return true;
}

float mermai_time_weighted_pos::calculate_weight(
    const mermai_validator_stake& stake,
    uint64_t current_time
) const {
    float time_mult = calculate_time_multiplier(current_time >= stake.lock_time ? current_time - stake.lock_time : 0);
    float perf_mult = calculate_performance_multiplier(stake);
    
    return stake.amount * time_mult * perf_mult;
}

std::string mermai_time_weighted_pos::select_block_proposer(uint64_t current_time) const {
    if (consensus_state.active_validators.empty()) {
        return "";
    }
    
    std::vector<std::pair<std::string, float>> weighted_validators;
    float total_weight = 0.0f;
    
    for (const auto& validator : consensus_state.active_validators) {
        float weight = calculate_weight(validator, current_time);
        weighted_validators.push_back({validator.address, weight});
        total_weight += weight;
    }
    
    if (total_weight <= 0.0f) {
        return consensus_state.active_validators[0].address;
    }
    
    std::ostringstream seed;
    seed << current_time;
    for (const auto& [address, weight] : weighted_validators) seed << address << weight;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX hash_context;
    SHA256_Init(&hash_context);
    const auto seed_value = seed.str();
    SHA256_Update(&hash_context, seed_value.data(), seed_value.size());
    SHA256_Final(digest, &hash_context);
    uint64_t random_seed = 0;
    std::memcpy(&random_seed, digest, sizeof(random_seed));
    float random_value = static_cast<float>(std::fmod(static_cast<double>(random_seed), static_cast<double>(total_weight)));
    float cumulative = 0.0f;
    
    for (const auto& [address, weight] : weighted_validators) {
        cumulative += weight;
        if (random_value <= cumulative) {
            return address;
        }
    }
    
    return weighted_validators.back().first;
}

void mermai_time_weighted_pos::finalize_block(
    const std::shared_ptr<mermai_block>& block,
    const std::string& proposer_address
) {
    if (!block) return;
    
    auto rewards = mermai_reward_calculator::calculate_block_rewards(
        block, block->height, *this
    );
    
    for (const auto& reward : rewards) {
        pending_rewards[reward.recipient] += reward.amount;
    }
    
    for (auto& v : consensus_state.active_validators) {
        if (v.address == proposer_address) {
            v.last_proposal = block->timestamp;
            break;
        }
    }
}

void mermai_time_weighted_pos::slash_validator(
    const std::string& address,
    float penalty_percentage,
    const std::string& reason
) {
    penalty_percentage = std::min(1.0f, std::max(0.0f, penalty_percentage));
    consensus_state.slash_validator(address, penalty_percentage);
    std::cout << "[SLASH] Validator " << address << " slashed by " 
              << (penalty_percentage * 100.0f) << "% for reason: " << reason << std::endl;
}


void mermai_time_weighted_pos::apply_inactivity_penalty(
    const std::string& address,
    float penalty_percentage
) {
    penalty_percentage = std::min(1.0f, std::max(0.0f, penalty_percentage / 100.0f));
    for (auto& v : consensus_state.active_validators) {
        if (v.address == address) {
            uint64_t reduction = static_cast<uint64_t>(v.amount * penalty_percentage);
            if (reduction >= v.amount) {
                v.amount = 0;
            } else {
                v.amount -= reduction;
            }
            std::cout << "[INACTIVITY] Validator " << address
                      << " penalised by " << (penalty_percentage * 100.0f)
                      << "%, new stake=" << v.amount << std::endl;
            return;
        }
    }
    std::cerr << "[INACTIVITY] Validator " << address << " not found for penalty" << std::endl;
}

bool mermai_time_weighted_pos::claim_rewards(const std::string& validator_address) {
    auto it = pending_rewards.find(validator_address);
    if (it == pending_rewards.end() || it->second == 0) {
        return false;
    }
    it->second = 0;
    return true;
}

uint64_t mermai_time_weighted_pos::get_pending_rewards(const std::string& validator_address) const {
    auto it = pending_rewards.find(validator_address);
    return (it != pending_rewards.end()) ? it->second : 0;
}

float mermai_time_weighted_pos::get_validator_weight(
    const std::string& address,
    uint64_t current_time
) const {
    for (const auto& v : consensus_state.active_validators) {
        if (v.address == address) {
            return calculate_weight(v, current_time);
        }
    }
    return 0.0f;
}

uint32_t mermai_time_weighted_pos::get_active_validator_count() const {
    return static_cast<uint32_t>(consensus_state.active_validators.size());
}

uint64_t mermai_time_weighted_pos::get_total_active_stake() const {
    uint64_t total = 0;
    for (const auto& v : consensus_state.active_validators) {
        total += v.amount;
    }
    return total;
}

float mermai_time_weighted_pos::calculate_time_multiplier(uint64_t stake_age_seconds) const {
    constexpr uint64_t seconds_per_day = 86400;
    uint64_t days = stake_age_seconds / seconds_per_day;
    
    if (days < 1) return 1.0f;
    if (days < 30) return 1.0f + (days * 0.001f);
    if (days < 365) return 1.03f + ((days - 30) * 0.001f);
    return 1.365f + std::min(0.1f, (days - 365) * 0.0001f);
}

float mermai_time_weighted_pos::calculate_performance_multiplier(
    const mermai_validator_stake& stake
) const {
    (void)stake;
    return 1.0f;
}

// ============================================================================
// QUORUM FINALITY COLLECTOR IMPLEMENTATION
// ============================================================================

bool mermai_quorum_collector::add_vote(const mermai_block_vote& vote, const mermai_time_weighted_pos& consensus) {
    if (!vote.verify_signature()) return false;

    // Check that voter is a registered validator with matching public key
    bool validator_found = false;
    uint64_t voter_stake = 0;
    for (const auto& val : consensus.get_consensus_state().active_validators) {
        if (val.address == vote.validator_address) {
            if (!val.public_key.empty() && val.public_key != vote.validator_public_key) {
                return false;
            }
            validator_found = true;
            voter_stake = val.amount;
            break;
        }
    }
    if (!validator_found || voter_stake == 0) return false;

    std::lock_guard<std::mutex> lock(quorum_mutex);
    auto& votes = block_votes[vote.block_hash];
    for (const auto& existing : votes) {
        if (existing.validator_address == vote.validator_address) {
            return true; // duplicate vote already counted
        }
    }

    votes.push_back(vote);
    vote_stake_totals[vote.block_hash] += voter_stake;

    // Check 2/3+ stake finality threshold (66.67% of total active stake)
    const uint64_t total_stake = consensus.get_total_active_stake();
    if (total_stake > 0 && vote_stake_totals[vote.block_hash] * 3 >= total_stake * 2) {
        finalized_blocks.insert(vote.block_hash);
    }

    return true;
}

bool mermai_quorum_collector::is_block_finalized(const std::string& block_hash) const {
    std::lock_guard<std::mutex> lock(quorum_mutex);
    return finalized_blocks.find(block_hash) != finalized_blocks.end();
}

uint64_t mermai_quorum_collector::get_vote_stake(const std::string& block_hash) const {
    std::lock_guard<std::mutex> lock(quorum_mutex);
    auto it = vote_stake_totals.find(block_hash);
    return (it != vote_stake_totals.end()) ? it->second : 0;
}

std::vector<mermai_block_vote> mermai_quorum_collector::get_votes(const std::string& block_hash) const {
    std::lock_guard<std::mutex> lock(quorum_mutex);
    auto it = block_votes.find(block_hash);
    return (it != block_votes.end()) ? it->second : std::vector<mermai_block_vote>{};
}

void mermai_quorum_collector::clear() {
    std::lock_guard<std::mutex> lock(quorum_mutex);
    block_votes.clear();
    vote_stake_totals.clear();
    finalized_blocks.clear();
}

// ============================================================================
// PROPOSAL VALIDATOR IMPLEMENTATION
// ============================================================================

std::map<std::pair<uint32_t, uint64_t>, std::string> mermai_proposal_validator::seen_proposals;
std::mutex mermai_proposal_validator::proposal_mutex;

void mermai_proposal_validator::reset_proposals() {
    std::lock_guard<std::mutex> lock(proposal_mutex);
    seen_proposals.clear();
}

bool mermai_proposal_validator::validate_proposal(
    const std::shared_ptr<mermai_block>& block,
    mermai_time_weighted_pos& consensus,
    uint64_t current_time
) {
    if (!block) return false;

    constexpr uint64_t block_interval_seconds = 10;
    const uint64_t slot_time = current_time - (current_time % block_interval_seconds);
    const std::string expected_proposer = consensus.select_block_proposer(slot_time);
    if (expected_proposer.empty() || block->validator_address != expected_proposer) return false;

    const std::string block_hash_hex = hash_to_hex(block->calculate_hash());

    // Equivocation (double-proposal) check
    {
        std::lock_guard<std::mutex> lock(proposal_mutex);
        const auto key = std::make_pair(block->height, slot_time);
        auto it = seen_proposals.find(key);
        if (it != seen_proposals.end()) {
            if (it->second != block_hash_hex) {
                // Equivocation detected! Slashes validator for double proposal
                consensus.slash_validator(expected_proposer, 0.5f, "Equivocation: double proposal at height " + std::to_string(block->height));
                return false;
            }
        } else {
            seen_proposals[key] = block_hash_hex;
        }
    }

    for (const auto& validator : consensus.get_consensus_state().active_validators) {
        if (validator.address != expected_proposer) continue;
        const uint64_t age = slot_time >= validator.lock_time ? slot_time - validator.lock_time : 0;
        
        // Strict validator signature validation: if validator has a key, proposal signature must be valid
        const bool signature_valid = validator.public_key.empty() ||
            (block->validator_public_key == validator.public_key && block->verify_validator_signature());
            
        return block->validator_stake == validator.amount && block->validator_age == age && signature_valid &&
               verify_proposer_is_authorized(expected_proposer, consensus.calculate_weight(validator, slot_time), slot_time);
    }
    return false;
}

bool mermai_proposal_validator::validate_signature(const mermai_transaction& tx) {
    return tx.verify();
}

bool mermai_proposal_validator::verify_proposer_is_authorized(
    const std::string& proposer,
    float validator_weight,
    uint64_t current_time
) {
    (void)current_time;
    return validator_weight > 0.0f && !proposer.empty();
}

// ============================================================================
// REWARD CALCULATOR IMPLEMENTATION
// ============================================================================

std::vector<mermai_block_reward> mermai_reward_calculator::calculate_block_rewards(
    const std::shared_ptr<mermai_block>& block,
    uint32_t block_height,
    const mermai_time_weighted_pos& consensus
) {
    (void)consensus;
    std::vector<mermai_block_reward> rewards;
    
    uint64_t base = get_base_reward(block_height);
    rewards.push_back({block->validator_address, base, "block_proposal"});
    
    auto fee_rewards = distribute_fees(block->transactions, block->validator_address);
    rewards.insert(rewards.end(), fee_rewards.begin(), fee_rewards.end());
    
    return rewards;
}

uint64_t mermai_reward_calculator::get_base_reward(uint32_t block_height) {
    uint64_t reward = 50000000;
    uint32_t halving_period = 210000;
    uint32_t halvings = block_height / halving_period;
    
    for (uint32_t i = 0; i < halvings && reward > 0; i++) {
        reward /= 2;
    }
    
    return reward;
}

std::vector<mermai_block_reward> mermai_reward_calculator::distribute_fees(
    const std::vector<mermai_transaction>& transactions,
    const std::string& proposer
) {
    std::vector<mermai_block_reward> rewards;
    uint64_t total_fees = 0;
    
    for (const auto& tx : transactions) {
        if (UINT64_MAX - total_fees < tx.fee) total_fees = UINT64_MAX;
        else total_fees += tx.fee;
    }
    
    if (total_fees > 0) {
        rewards.push_back({proposer, total_fees, "transaction_fees"});
    }
    
    return rewards;
}

} // namespace mermai
