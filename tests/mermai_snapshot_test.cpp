#include "mermai/mermai_db.hpp"
#include "mermai/mermai_blockchain.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <cstdio>

int main() {
    std::cout << "Starting Mermai Phase 4 - State Snapshot tests..." << std::endl;
    const std::string db_file = "mermai_snapshot_test.db";
    std::remove(db_file.c_str());

    auto db = std::make_shared<mermai::mermai_db>(db_file);
    assert(db->initialize());

    // Setup state: accounts and UTXOs
    assert(db->save_account({"alice", 5000000, 0, 1000}));
    assert(db->save_account({"bob", 3000000, 0, 1000}));
    assert(db->save_account({"carol", 2000000, 0, 1000}));
    assert(db->save_unspent_output({"genesis-utxo-1", 0, "alice", 5000000}));
    assert(db->save_unspent_output({"genesis-utxo-2", 0, "bob", 3000000}));
    assert(db->save_unspent_output({"genesis-utxo-3", 0, "carol", 2000000}));

    // Create a state root snapshot at height 0
    const std::string root_0 = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";
    assert(db->save_state_root(0, root_0));

    // Apply a block
    auto block = std::make_shared<mermai::mermai_block>(1);
    block->prev_block_hash = "genesis";
    block->timestamp = 1000000;
    block->validator_address = "val_alice";
    mermai::mermai_transaction tx;
    tx.id = "snapshot-tx-1";
    tx.timestamp = 1000000;
    tx.outputs.push_back({"dave", 1000000});
    block->transactions.push_back(tx);
    assert(db->apply_block(block));
    assert(db->save_block(block));

    // Save state root at height 1
    const std::string root_1 = "b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3";
    assert(db->save_state_root(1, root_1));

    // Verify state roots
    assert(db->get_state_root(0) == root_0);
    assert(db->get_state_root(1) == root_1);
    std::cout << "  [OK] State roots saved and retrieved at heights 0 and 1" << std::endl;

    // Verify snapshot contains UTXOs
    mermai::mermai_db::UnspentOutput utxo;
    assert(db->get_unspent_output("genesis-utxo-1", 0, utxo));
    assert(utxo.address == "alice" && utxo.amount == 5000000);
    assert(db->get_unspent_output("snapshot-tx-1", 0, utxo));
    assert(utxo.address == "dave" && utxo.amount == 1000000);
    std::cout << "  [OK] UTXO state preserved and queryable after snapshot" << std::endl;

    // Verify block height tracking
    assert(db->get_latest_block_height() == 1);
    std::cout << "  [OK] Latest block height tracked correctly: " << db->get_latest_block_height() << std::endl;

    // Simulate fast-sync: new node loading snapshot  
    db->sync();
    auto block_back = db->get_block_by_height(1);
    assert(block_back != nullptr);
    assert(block_back->validator_address == "val_alice");
    std::cout << "  [OK] Block restored from snapshot state" << std::endl;

    std::remove(db_file.c_str());
    std::cout << "\nAll Phase 4 State Snapshot tests passed successfully!" << std::endl;
    return 0;
}
