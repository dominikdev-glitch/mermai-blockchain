#include "mermai/mermai_blockchain.hpp"
#include "mermai/mermai_consensus.hpp"
#include "mermai/mermai_db.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <cstdio>

int main() {
    std::cout << "Starting mermai_restart and state recovery tests..." << std::endl;
    const std::string db_file = "mermai_restart_test.db";
    std::remove(db_file.c_str());

    // --- SESSION 1: Initialize, commit blocks & validators, then shut down ---
    {
        auto db = std::make_shared<mermai::mermai_db>(db_file);
        assert(db->initialize());

        // Register validators in DB
        mermai::mermai_validator_stake val1{"val_alice", 5000000, 100, 0, 5000000.0f, {0x01, 0x02}};
        mermai::mermai_validator_stake val2{"val_bob", 3000000, 100, 0, 3000000.0f, {0x03, 0x04}};
        assert(db->save_validator(val1));
        assert(db->save_validator(val2));

        // Save account balance
        assert(db->save_account({"alice", 10000, 1, 500}));

        // Apply a block
        auto block = std::make_shared<mermai::mermai_block>(1);
        block->prev_block_hash = "genesis_hash";
        block->timestamp = 1000;
        block->validator_address = "val_alice";
        
        mermai::mermai_transaction tx;
        tx.id = "tx-restart-1";
        tx.timestamp = 1000;
        tx.outputs.push_back({"bob", 500});
        block->transactions.push_back(tx);

        assert(db->apply_block(block));
        assert(db->save_block(block));
        assert(db->save_state_root(1, "state_root_1"));
        db->sync();
    } // DB closed (node simulated shutdown)

    // --- SESSION 2: Reboot node and verify state restoration from SQLite ---
    {
        auto db = std::make_shared<mermai::mermai_db>(db_file);
        assert(db->initialize());

        // 1. Verify block persistence
        const uint32_t height = db->get_latest_block_height();
        assert(height == 1);
        auto loaded_block = db->get_block_by_height(1);
        assert(loaded_block != nullptr);
        assert(loaded_block->validator_address == "val_alice");

        // 2. Verify account & UTXO persistence
        auto acct = db->get_account("alice");
        assert(acct.balance == 10000);
        mermai::mermai_db::UnspentOutput utxo;
        assert(db->get_unspent_output("tx-restart-1", 0, utxo));
        assert(utxo.address == "bob" && utxo.amount == 500);

        // 3. Verify validator registry restoration
        auto validators = db->get_all_validators();
        assert(validators.size() == 2);

        auto consensus = std::make_shared<mermai::mermai_time_weighted_pos>();
        for (const auto& v : validators) {
            // Use restore_validator to skip re-validation of already-verified keys
            consensus->restore_validator(v);
        }
        assert(consensus->get_active_validator_count() == 2);
        assert(consensus->get_total_active_stake() == 8000000);

        // 4. Verify state root recovery
        std::string root = db->get_state_root(1);
        assert(root == "state_root_1");
    }

    std::remove(db_file.c_str());
    std::cout << "All mermai_restart state recovery tests passed successfully!" << std::endl;
    return 0;
}
