#include "mermai/mermai_governance.hpp"
#include <iostream>

namespace mermai {

uint64_t mermai_governance_engine::create_proposal(
    const std::string& proposer,
    const std::string& title,
    const std::string& description,
    proposal_type type,
    uint32_t current_block,
    const std::string& target_param,
    uint64_t new_val
) {
    if (proposer.empty() || title.empty()) return 0;

    uint64_t pid = next_proposal_id++;
    mermai_proposal p;
    p.id = pid;
    p.proposer = proposer;
    p.title = title;
    p.description = description;
    p.type = type;
    p.status = proposal_status::ACTIVE;
    p.start_block = current_block;
    p.end_block = current_block + voting_period_blocks;
    p.target_parameter = target_param;
    p.new_parameter_value = new_val;

    proposals[pid] = p;
    return pid;
}

bool mermai_governance_engine::vote(
    uint64_t proposal_id,
    const std::string& voter,
    uint64_t stake,
    bool support,
    uint32_t current_block
) {
    auto it = proposals.find(proposal_id);
    if (it == proposals.end()) return false;
    auto& p = it->second;

    if (p.status != proposal_status::ACTIVE) return false;
    if (current_block > p.end_block) return false;
    if (p.voter_records.count(voter) > 0) return false;

    p.voter_records[voter] = support;
    if (support) {
        p.votes_yes += stake;
    } else {
        p.votes_no += stake;
    }
    return true;
}

bool mermai_governance_engine::tally_and_execute(
    uint64_t proposal_id,
    uint32_t current_block,
    uint64_t total_active_stake,
    mermai_time_weighted_pos* /* consensus */
) {
    auto it = proposals.find(proposal_id);
    if (it == proposals.end()) return false;
    auto& p = it->second;

    if (p.status != proposal_status::ACTIVE) return false;
    if (current_block < p.end_block) return false;

    uint64_t total_voted = p.votes_yes + p.votes_no + p.votes_abstain;
    if (total_active_stake > 0) {
        float participation = static_cast<float>(total_voted) / static_cast<float>(total_active_stake);
        if (participation < quorum_threshold) {
            p.status = proposal_status::REJECTED;
            return false;
        }
    }

    if (total_voted > 0 && static_cast<float>(p.votes_yes) / static_cast<float>(total_voted) > pass_threshold) {
        p.status = proposal_status::PASSED;
        if (p.type == proposal_type::PARAMETER_CHANGE) {
            p.status = proposal_status::EXECUTED;
            std::cout << "[GOVERNANCE] Executed proposal #" << p.id << ": " << p.target_parameter << " -> " << p.new_parameter_value << std::endl;
        }
        return true;
    } else {
        p.status = proposal_status::REJECTED;
        return false;
    }
}

const mermai_proposal* mermai_governance_engine::get_proposal(uint64_t proposal_id) const {
    auto it = proposals.find(proposal_id);
    return (it != proposals.end()) ? &it->second : nullptr;
}

std::vector<mermai_proposal> mermai_governance_engine::get_all_proposals() const {
    std::vector<mermai_proposal> res;
    for (const auto& kv : proposals) res.push_back(kv.second);
    return res;
}

} // namespace mermai
