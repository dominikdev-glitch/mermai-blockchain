#include "mermai/mermai_governance.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Starting Mermai Phase 6 - On-Chain Governance tests..." << std::endl;

    mermai::mermai_governance_engine gov;

    uint64_t pid = gov.create_proposal(
        "val_alice",
        "Increase Block Gas Limit",
        "Proposal to increase block gas limit to 30,000,000",
        mermai::proposal_type::PARAMETER_CHANGE,
        100,
        "block_gas_limit",
        30000000
    );
    assert(pid == 1);
    assert(gov.get_proposal_count() == 1);
    const auto* p = gov.get_proposal(1);
    assert(p != nullptr);
    assert(p->status == mermai::proposal_status::ACTIVE);
    assert(p->end_block == 200);
    std::cout << "  [OK] Proposal #1 created: " << p->title << std::endl;

    assert(gov.vote(1, "val_alice", 5000000, true, 110));
    assert(gov.vote(1, "val_bob", 3000000, true, 120));
    assert(gov.vote(1, "val_carol", 2000000, false, 130));

    assert(!gov.vote(1, "val_alice", 5000000, false, 140));
    std::cout << "  [OK] Stake-weighted votes recorded: 8M Yes vs 2M No" << std::endl;

    assert(!gov.tally_and_execute(1, 150, 10000000));
    assert(gov.get_proposal(1)->status == mermai::proposal_status::ACTIVE);
    std::cout << "  [OK] Tally before voting period end rejected" << std::endl;

    assert(gov.tally_and_execute(1, 201, 10000000));
    assert(gov.get_proposal(1)->status == mermai::proposal_status::EXECUTED);
    std::cout << "  [OK] Proposal passed & executed (80% Yes >= 50% threshold, 100% participation >= 40% quorum)" << std::endl;

    uint64_t pid2 = gov.create_proposal(
        "val_bob",
        "Grant to Community Fund",
        "Fund 50,000 MRM for community hackathon",
        mermai::proposal_type::COMMUNITY_GRANT,
        210
    );
    assert(gov.vote(pid2, "voter_small", 1000000, true, 220));
    assert(!gov.tally_and_execute(pid2, 320, 10000000));
    assert(gov.get_proposal(pid2)->status == mermai::proposal_status::REJECTED);
    std::cout << "  [OK] Proposal rejected due to low quorum participation" << std::endl;

    std::cout << "All On-Chain Governance tests passed successfully!" << std::endl;
    return 0;
}
