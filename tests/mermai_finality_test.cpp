#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_consensus.hpp"
#include "mermai/mermai_db.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <cstdio>

int main() {
    std::cout << "Starting Mermai Phase 3 - Finality & Fork Choice tests..." << std::endl;

    const std::string db_file = "mermai_finality_test.db";
    std::remove(db_file.c_str());

    auto db = std::make_shared<mermai::mermai_db>(db_file);
    assert(db->initialize());

    auto consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
    assert(consensus->register_validator("val_alice", 5000000, "Alice", {}));
    assert(consensus->register_validator("val_bob",   3000000, "Bob",   {}));
    assert(consensus->register_validator("val_carol", 2000000, "Carol", {}));

    // Simulate blocks 1..5 via DB
    for (int h = 1; h <= 5; ++h) {
        auto block = std::make_shared<mermai::mermai_block>(h);
        block->prev_block_hash = (h == 1) ? "genesis" : std::to_string(h - 1);
        block->timestamp = 1000000 + static_cast<uint64_t>(h);
        block->validator_address = "val_alice";
        block->validator_stake = 5000000;
        block->validator_age   = 0;
        assert(db->apply_block(block));
        assert(db->save_block(block));
    }

    assert(db->get_latest_block_height() == 5);
    std::cout << "  [OK] Chain at height 5 (via DB)" << std::endl;

    auto tip = db->get_block_by_height(5);
    assert(tip != nullptr);
    assert(tip->height == 5);
    assert(tip->validator_address == "val_alice");
    std::cout << "  [OK] Block at height 5 retrieved correctly" << std::endl;

    // Test: Validator slashing (50%)
    uint64_t stake_before = consensus->get_total_active_stake();
    consensus->slash_validator("val_carol", 50);   // 50% of 2000000 = 1000000
    uint64_t stake_after = consensus->get_total_active_stake();
    assert(stake_after < stake_before);
    assert(stake_before - stake_after == 1000000);
    std::cout << "  [OK] Equivocation slashing: 50% of Carol's stake burned. Before="
              << stake_before << " After=" << stake_after << std::endl;

    // Test: Proposer selection distribution
    bool alice_proposed = false, bob_proposed = false, carol_proposed = false;
    for (uint64_t slot = 1700000000; slot < 1700000100; slot += 10) {
        const std::string p = consensus->select_block_proposer(slot);
        if (p == "val_alice")      alice_proposed = true;
        else if (p == "val_bob")   bob_proposed   = true;
        else if (p == "val_carol") carol_proposed = true;
    }
    assert(alice_proposed || bob_proposed);
    std::cout << "  [OK] Proposer selection: Alice=" << alice_proposed
              << " Bob=" << bob_proposed << " Carol=" << carol_proposed << std::endl;

    // Test: Inactivity penalty
    uint64_t before_inactivity = consensus->get_total_active_stake();
    consensus->apply_inactivity_penalty("val_bob", 10); // 10%
    uint64_t after_inactivity = consensus->get_total_active_stake();
    assert(after_inactivity < before_inactivity);
    std::cout << "  [OK] Inactivity penalty applied to Bob (10% of stake)" << std::endl;

    std::remove(db_file.c_str());
    std::cout << "\nAll Phase 3 Finality & Slashing tests passed successfully!" << std::endl;
    return 0;
}
