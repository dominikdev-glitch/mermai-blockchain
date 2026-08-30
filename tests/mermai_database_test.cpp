#include "mermai/mermai_db.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>

int main() {
    const std::string path = "mermai_database_test.db";
    std::remove(path.c_str());

    mermai::mermai_db database(path);
    assert(database.initialize());

    mermai::mermai_db::Account account{"alice", 1250, 4, 99};
    assert(database.save_account(account));
    const auto restored_account = database.get_account("alice");
    assert(restored_account.address == "alice");
    assert(restored_account.balance == 1250 && restored_account.nonce == 4);
    assert(database.update_balance("alice", 1500));
    assert(database.increment_nonce("alice"));
    assert(database.get_account("alice").balance == 1500);
    assert(database.get_account("alice").nonce == 5);

    mermai::mermai_validator_stake validator{"validator", 1000, 10, 20, 2.5f};
    assert(database.save_validator(validator));
    assert(database.get_validator("validator").amount == 1000);
    assert(database.update_validator_stake("validator", 2000));
    assert(database.get_validator("validator").amount == 2000);

    assert(database.save_contract_storage("contract", "key", {1, 2, 3}));
    assert(database.get_contract_storage("contract", "key") == std::vector<uint8_t>({1, 2, 3}));
    assert(database.save_state_root(7, "root-7"));
    assert(database.get_state_root(7) == "root-7");

    mermai::mermai_transaction transaction;
    transaction.id = "transaction-1";
    transaction.timestamp = 100;
    transaction.inputs.push_back({"previous", 0, {1}, {2}});
    transaction.outputs.push_back({"alice", 100});
    assert(database.save_transaction(transaction));
    assert(database.get_account_transactions("alice").size() == 1);
    assert(database.add_to_mempool(transaction));
    assert(database.get_mempool().size() == 1);
    assert(database.get_transaction("transaction-1").id == "transaction-1");
    assert(database.remove_from_mempool("transaction-1"));
    assert(database.get_mempool().empty());

    auto block = std::make_shared<mermai::mermai_block>(1);
    block->prev_block_hash = "genesis";
    block->transactions.push_back(transaction);
    assert(database.save_block(block));
    assert(database.get_latest_block_height() == 1);
    const auto restored_block = database.get_block_by_height(1);
    assert(restored_block && restored_block->transactions.size() == 1);
    assert(restored_block->transactions[0].id == "transaction-1");

    const auto root = mermai::mermai_merkle_tree::calculate_tree_hash({transaction});
    assert(!root.empty());
    assert(mermai::mermai_merkle_tree::verify_tree({transaction}, root) == root);
    assert(mermai::mermai_merkle_tree::verify_tree({transaction}, "wrong") == "");

    // ========== UTXO ROUND-TRIP ==========
    mermai::mermai_db::UnspentOutput utxo{"utxo-tx-1", 0, "bob", 500};
    assert(database.save_unspent_output(utxo));
    mermai::mermai_db::UnspentOutput fetched_utxo;
    assert(database.get_unspent_output("utxo-tx-1", 0, fetched_utxo));
    assert(fetched_utxo.address == "bob" && fetched_utxo.amount == 500);
    // Spend it and confirm it's gone
    assert(database.spend_unspent_output("utxo-tx-1", 0));
    mermai::mermai_db::UnspentOutput spent_check;
    assert(!database.get_unspent_output("utxo-tx-1", 0, spent_check));
    // Double-spend should fail (row no longer exists)
    assert(!database.spend_unspent_output("utxo-tx-1", 0));

    // ========== apply_block WITH COINBASE (zero-input) TRANSACTION ==========
    mermai::mermai_transaction coinbase_tx;
    coinbase_tx.id = "coinbase-tx-1";
    coinbase_tx.timestamp = 200;
    // No inputs — this is an issuance / coinbase transaction
    coinbase_tx.outputs.push_back({"carol", 1000});
    coinbase_tx.outputs.push_back({"dave",  500});

    auto coinbase_block = std::make_shared<mermai::mermai_block>(2);
    coinbase_block->prev_block_hash = "genesis";
    coinbase_block->timestamp = 200;
    coinbase_block->transactions.push_back(coinbase_tx);
    // apply_block should succeed even though the tx has no inputs
    assert(database.apply_block(coinbase_block));

    // Outputs must be in the UTXO set
    mermai::mermai_db::UnspentOutput carol_utxo, dave_utxo;
    assert(database.get_unspent_output("coinbase-tx-1", 0, carol_utxo));
    assert(carol_utxo.address == "carol" && carol_utxo.amount == 1000);
    assert(database.get_unspent_output("coinbase-tx-1", 1, dave_utxo));
    assert(dave_utxo.address == "dave" && dave_utxo.amount == 500);

    // ========== get_account_transactions — received side via UTXO join ==========
    // carol received from the coinbase block, dave did too
    const auto carol_txs = database.get_account_transactions("carol");
    assert(carol_txs.size() >= 1);
    bool found_coinbase = false;
    for (const auto& t : carol_txs) {
        if (t.id == "coinbase-tx-1") { found_coinbase = true; break; }
    }
    assert(found_coinbase);

    database.sync();
    std::remove(path.c_str());
    std::cout << "Database integration test passed.\n";
}
