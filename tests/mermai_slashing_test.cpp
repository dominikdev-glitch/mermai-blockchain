#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_consensus.hpp"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
    std::cout << "Starting Mermai Phase 3 - Slashing & Inactivity tests..." << std::endl;

    auto consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(consensus->register_validator("val_alice", 10000000, "Alice", {}));
    assert(consensus->register_validator("val_bob", 5000000, "Bob", {}));

    // Test 1: Slash 50%
    uint64_t total_before = consensus->get_total_active_stake();
    assert(total_before == 15000000);
    consensus->slash_validator("val_alice", 50);
    uint64_t total_after = consensus->get_total_active_stake();
    assert(total_before - total_after == 5000000); // 50% of 10M = 5M burned
    std::cout << "  [OK] 50% slash: stake reduced from " << total_before << " to " << total_after << std::endl;

    // Test 2: Full slash (100%)
    consensus->slash_validator("val_bob", 100);
    uint64_t total_after_full = consensus->get_total_active_stake();
    assert(total_after_full == total_after - 5000000); // Bob's 5M all gone
    std::cout << "  [OK] 100% slash: Bob fully burned" << std::endl;

    // Test 3: Inactivity decay
    auto consensus2 = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(consensus2->register_validator("val_carol", 10000000, "Carol", {}));
    consensus2->apply_inactivity_penalty("val_carol", 10); // 10%
    uint64_t stake_after_inactivity = consensus2->get_total_active_stake();
    assert(stake_after_inactivity == 9000000); // 10% of 10M = 1M deducted
    std::cout << "  [OK] Inactivity decay: 10% penalty applied, stake=" << stake_after_inactivity << std::endl;

    // Test 4: Repeated inactivity compounds
    consensus2->apply_inactivity_penalty("val_carol", 10);
    uint64_t after_second = consensus2->get_total_active_stake();
    assert(after_second == 8100000); // 10% of 9M = 900K deducted
    std::cout << "  [OK] Compound inactivity: stake=" << after_second << std::endl;

    std::cout << "\nAll Phase 3 Slashing & Inactivity tests passed successfully!" << std::endl;
    return 0;
}
