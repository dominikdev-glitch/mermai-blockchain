#include "mermai/mermai_mempool.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Starting mermai_mempool tests..." << std::endl;
    mermai::mermai_mempool pool(3); // Small capacity for eviction test

    mermai::mermai_transaction tx1;
    tx1.id = "tx-1";
    tx1.fee = 100;
    tx1.inputs.push_back({"prev-tx-1", 0, {}, {}});
    tx1.outputs.push_back({"alice", 50});

    assert(pool.add_transaction(tx1));
    assert(pool.size() == 1);
    assert(pool.has_transaction("tx-1"));
    assert(pool.is_utxo_spent("prev-tx-1", 0));

    // Duplicate tx rejection
    assert(!pool.add_transaction(tx1));

    // Double-spend rejection (same UTXO spent in another unconfirmed tx)
    mermai::mermai_transaction tx2;
    tx2.id = "tx-2";
    tx2.fee = 200;
    tx2.inputs.push_back({"prev-tx-1", 0, {}, {}}); // Conflict!
    tx2.outputs.push_back({"bob", 50});
    assert(!pool.add_transaction(tx2));

    // Add different valid txs
    mermai::mermai_transaction tx3;
    tx3.id = "tx-3";
    tx3.fee = 500;
    tx3.inputs.push_back({"prev-tx-2", 0, {}, {}});
    tx3.outputs.push_back({"carol", 100});
    assert(pool.add_transaction(tx3));

    mermai::mermai_transaction tx4;
    tx4.id = "tx-4";
    tx4.fee = 300;
    tx4.inputs.push_back({"prev-tx-3", 0, {}, {}});
    tx4.outputs.push_back({"dave", 100});
    assert(pool.add_transaction(tx4));

    assert(pool.size() == 3); // Max capacity reached

    // Tx with low fee (< 100) should be rejected
    mermai::mermai_transaction tx_low;
    tx_low.id = "tx-low";
    tx_low.fee = 50;
    tx_low.inputs.push_back({"prev-tx-4", 0, {}, {}});
    assert(!pool.add_transaction(tx_low));

    // Tx with high fee (> 100) should evict lowest (tx1 with fee 100)
    mermai::mermai_transaction tx_high;
    tx_high.id = "tx-high";
    tx_high.fee = 800;
    tx_high.inputs.push_back({"prev-tx-5", 0, {}, {}});
    assert(pool.add_transaction(tx_high));
    assert(pool.size() == 3);
    assert(!pool.has_transaction("tx-1")); // tx1 was evicted
    assert(pool.has_transaction("tx-high"));

    // Sorted by fee: tx-high (800), tx-3 (500), tx-4 (300)
    auto sorted_txs = pool.get_transactions();
    assert(sorted_txs.size() == 3);
    assert(sorted_txs[0].id == "tx-high");
    assert(sorted_txs[1].id == "tx-3");
    assert(sorted_txs[2].id == "tx-4");

    // Median fee test
    assert(pool.get_median_fee() == 500);

    // Remove transaction
    assert(pool.remove_transaction("tx-high"));
    assert(pool.size() == 2);
    assert(!pool.is_utxo_spent("prev-tx-5", 0));

    std::cout << "All mermai_mempool tests passed successfully!" << std::endl;
    return 0;
}
