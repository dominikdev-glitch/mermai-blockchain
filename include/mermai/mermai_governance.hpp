#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <memory>
#include "mermai/mermai_consensus.hpp"

namespace mermai {

enum class proposal_status {
    ACTIVE,
    PASSED,
    REJECTED,
    EXECUTED
};

enum class proposal_type {
    PARAMETER_CHANGE,
    TEXT_PROPOSAL,
    COMMUNITY_GRANT
};

struct mermai_proposal {
    uint64_t id = 0;
    std::string proposer;
    std::string title;
    std::string description;
    proposal_type type = proposal_type::TEXT_PROPOSAL;
    proposal_status status = proposal_status::ACTIVE;

    std::string target_parameter;
    uint64_t new_parameter_value = 0;

    uint32_t start_block = 0;
    uint32_t end_block = 0;

    uint64_t votes_yes = 0;
    uint64_t votes_no = 0;
    uint64_t votes_abstain = 0;

    std::map<std::string, bool> voter_records;
};

class mermai_governance_engine {
private:
    std::map<uint64_t, mermai_proposal> proposals;
    uint64_t next_proposal_id = 1;
    uint64_t min_proposal_deposit = 100000;
    uint32_t voting_period_blocks = 100;
    float quorum_threshold = 0.40f;
    float pass_threshold = 0.50f;

public:
    mermai_governance_engine() = default;

    uint64_t create_proposal(
        const std::string& proposer,
        const std::string& title,
        const std::string& description,
        proposal_type type,
        uint32_t current_block,
        const std::string& target_param = "",
        uint64_t new_val = 0
    );

    bool vote(
        uint64_t proposal_id,
        const std::string& voter,
        uint64_t stake,
        bool support,
        uint32_t current_block
    );

    bool tally_and_execute(
        uint64_t proposal_id,
        uint32_t current_block,
        uint64_t total_active_stake,
        mermai_time_weighted_pos* consensus = nullptr
    );

    const mermai_proposal* get_proposal(uint64_t proposal_id) const;
    std::vector<mermai_proposal> get_all_proposals() const;
    size_t get_proposal_count() const { return proposals.size(); }
};

} // namespace mermai
