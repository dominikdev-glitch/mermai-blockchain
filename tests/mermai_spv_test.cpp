#include "mermai/mermai_light_client.hpp"
#include "mermai/mermai_merkle.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Starting Mermai Phase 6 - Light Client / SPV tests..." << std::endl;

    mermai::mermai_light_client client;

    std::vector<std::string> txs_block1 = {
        "tx_hash_1111111111111111111111111111111111111111111111111111111111111111",
        "tx_hash_2222222222222222222222222222222222222222222222222222222222222222",
        "tx_hash_3333333333333333333333333333333333333333333333333333333333333333"
    };
    mermai::mermai_merkle_tree tree1(txs_block1);
    std::string root1 = tree1.get_root();

    mermai::mermai_block_header h1;
    h1.height = 1;
    h1.hash = "block_hash_001";
    h1.prev_block_hash = "genesis";
    h1.merkle_root = root1;
    h1.timestamp = 1000000;
    h1.validator_address = "val_alice";

    assert(client.sync_header(h1));
    assert(client.get_latest_height() == 1);
    assert(client.get_header_count() == 1);
    std::cout << "  [OK] Block 1 header synced into light client" << std::endl;

    for (size_t i = 0; i < txs_block1.size(); ++i) {
        auto proof = tree1.generate_proof(i);
        assert(client.verify_transaction_inclusion(1, txs_block1[i], proof));
    }
    std::cout << "  [OK] SPV transaction inclusion proofs verified for all 3 transactions" << std::endl;

    auto forged_proof = tree1.generate_proof(0);
    assert(!client.verify_transaction_inclusion(1, "fake_tx_hash_00000000000000000000000000000000", forged_proof));
    std::cout << "  [OK] Fake transaction inclusion proof rejected" << std::endl;

    mermai::mermai_block_header h2;
    h2.height = 2;
    h2.hash = "block_hash_002";
    h2.prev_block_hash = "block_hash_001";
    h2.merkle_root = "some_root_002";
    h2.timestamp = 1000010;
    h2.validator_address = "val_bob";

    assert(client.sync_header(h2));
    assert(client.get_latest_height() == 2);
    std::cout << "  [OK] Block 2 header synced, light client tip height: 2" << std::endl;

    std::cout << "All Light Client / SPV tests passed successfully!" << std::endl;
    return 0;
}
